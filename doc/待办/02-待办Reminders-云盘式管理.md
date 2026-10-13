# 02 待办（Reminders）—— 云盘式管理

前提：01 的 Qt 侧接入**已完成**（2026-10-13，commit bfe5827）—— 旁路已能稳定供数，
界面已能消费 payload。

> ★ 2026-10-13 范围收敛：**先做待办，备忘录暂缓。**
> 待办是完整读写 + 增量游标 → 可以做成云盘式双向管理；
> 备忘录 pyicloud 侧**没有任何写方法** → 只能只读镜像，等只读需求真出现再说。
> 备忘录的结论保留在文末，不删。

---

## 一、凭据从哪来（本节回答"要不要做 iCloud 网页登录界面"）

**答案：不需要网页登录界面，也不要复用 iCloud for Windows 的凭据。**

### 1.1 复用官方客户端凭据 —— 实地查证后否决

在本机实测（iCloud for Windows 为 UWP 版，AUMID `AppleInc.iCloud_nzyj5cx40ttqa`）：

| 查的东西 | 实际结果 |
|---|---|
| `Windows 凭据管理器` | **没有任何 Apple / iCloud 条目**（只有 git、远程桌面、Office、OneDrive 等） |
| `com.apple.AOSKit.plist` | 找到。但值是不透明 blob：`01 00 00 00` + `df9d8cd0-1501-11d1-8c7a-00c04fc297eb` → **DPAPI 加密**（`CryptProtectData` 输出格式） |
| `LocalState` / `RoamingState` / `Settings` | 无账号凭据 |
| `keychain-2.db` | 是**同步过来的 iCloud 钥匙串**（用户密码库），不是登录凭据 |

否决理由（三条，任一条都够）：

1. **格式不对**。AOSKit 里是**原生 AOS token**（`mmeAuthToken` / `cloudKitToken` /
   各 dataclass 服务 token），而 CloudKit **Web Services** 需要的是网页会话的
   `X-APPLE-WEBAUTH-TOKEN` cookie 族 + DSID。两者不是一套东西。
2. **格式无契约、随时变**。AOSKit 的 blob 是 Apple 私有序列化 + DPAPI，无文档。
   把核心能力押在**另一个进程的私有存储格式**上，Apple 一次更新就断，
   且断的时候我们连"它变了"都难判断。
3. **收益极小**。复用凭据最多省掉**一次**「输账号密码 + 一次 2FA」。
   而 pyicloud 的 `trust_session()` 之后**数周都不用再输**。
   用一个随时会断的私有格式，去换一次输入 —— 不划算。

### 1.2 也不做网页登录界面

pyicloud 自带原生登录（SRP-6a + GSA），UI 只需要一个**普通表单**：

- Apple ID（记住）
- 密码（首次）
- 6 位 2FA 码（首次，或会话失效时）

不是网页、不是 WebView、不嵌入浏览器。**107 MB 的 Qt WebEngine 不用付。**

### 1.3 App 专用密码（第三条路，成本最低）

用 `appleid.apple.com` 生成的 App 专用密码，配 CalDAV —— **零 2FA、口令可一键吊销**。
代价：只对**没被升级到 CloudKit 的旧清单**有效，可能一个清单都读不到。必须实测。

---

## 二、取数路径：两条，先探后选

**不要猜。** 用只读探针实测，结果决定走哪条。

```
# 路 B：CalDAV + App 专用密码（推荐先试，成本最低，不用交主号密码）
sidecar/runtime/Scripts/python.exe sidecar/probe_reminders.py caldav   --user you@icloud.com

# 路 A：pyicloud 原生登录（SRP-6a + 2FA）+ CloudKit
sidecar/runtime/Scripts/python.exe sidecar/probe_reminders.py cloudkit --user you@icloud.com

# 两条都探
sidecar/runtime/Scripts/python.exe sidecar/probe_reminders.py both     --user you@icloud.com
```

探针**只读**：只调 `lists()` / `list_reminders()` / CalDAV 的 `get_todos()`，
不碰任何写接口。口令走 `getpass`（不回显），只打印掩码。带 `--china/--no-china`，
默认从本机 `iCloudWinPref.plist` 自动判断（本机已判为**云上贵州**）。

| 探针结果 | 走哪条 | 后果 |
|---|---|---|
| 路 B ✓ | **CalDAV** | 零 2FA、协议公开(RFC 4791)、库成熟(caldav/icalendar)、口令可吊销。维护成本最低 |
| 路 B ✗、路 A ✓ | **CloudKit** | 功能最全(含写 + 增量游标)；需锁 pyicloud 版本 + 把探针留作回归 |
| 都 ✗ | 停下来看错误 | 不要继续写上层代码 |

**路 A 的已知限制**（写进代码前必须记住）：
`create()` 传 naive datetime 会被当 **UTC** 解释（差 8 小时），一律传带时区 datetime。

---

## 三、待办实现计划（选定路径之后）

数据模型已经对齐（pyicloud 的 `Reminder` 是 pydantic 模型，字段齐）：

```
id, list_id, title, desc, completed, completed_date, due_date, start_date,
priority, flagged, all_day, deleted, time_zone,
alarm_ids, hashtag_ids, attachment_ids, recurrence_rule_ids, parent_reminder_id,
created, modified, record_change_tag
```

[ ] 分层：**取数 + 写入都归 Python 旁路**（pyicloud 就在 Python 侧，C++ 不该重复实现）；
    C++ 只做**转发 + 本地镜像 + 状态机**；QML 只做界面

[ ] 旁路新增 NDJSON 命令：
    `reminders.lists` / `reminders.list` / `reminders.get` /
    `reminders.create` / `reminders.update` / `reminders.delete` / `reminders.sync`

[ ] 本地镜像：旁路维护一份本地缓存（SQLite 或 JSON），
    拉用 `sync_cursor()` + `iter_changes(since=cursor)` 只取变化

[ ] tombstone：`iter_changes` 里 `event.reminder is None` 时**只能靠 `event.reminder_id`
    删本地** —— 不处理会留下幽灵条目

[ ] 写入策略：本地改动先落本地（标 dirty）→ 后台推云端 → 成功清 dirty / 失败重试；
    界面立刻反映，不等网络

[ ] 冲突：本地与云端同一条都改了 → 先只做"提示 + 以云端为准"，**不做自动合并**

[ ] 到期提醒：只在本程序内提示（不写回系统日历），图标与文案和"卡死通知"区分开

[ ] 时间：一律带时区（见上）

---

## 四、界面接入

[ ] 侧边栏新增「待办」；沿用现有 NavItem 与 `navOrder`，默认排在四类同步之后

[ ] 待办页：清单分栏 + 条目列表 + 勾选（勾选立刻反映本地，后台再推）

[ ] 空状态与错误态：未登录 / 需要 2FA / 网络失败 / 会话过期，四种各有明确文案，不留白屏

[ ] 与监控解耦：这页的数据源是旁路，官方 iCloud 进程没在跑也应能正常显示

---

## 五、备忘录（Notes）—— 暂缓，结论保留

**当前不做。** 结论记录在此，避免重复调研：

- 能读：文件夹 / 列表 / 正文 / 附件，能 `render_note()` 转 HTML
- **不能写**：pyicloud Notes 服务**一个写方法都没有**；要写只能走 `api.notes.raw`
  直连 CloudKit 自造压缩 protobuf，工程量大
- 若将来只读需求出现，按此实现即可：`recents` / `folders` / `in_folder` / `iter_all`
  + `render_note()`；附件先只显示元数据；`NoteLockedError` → 标"已锁定，无法读取"

---

## 六、云盘与照片 —— 暂缓

- iCloud Drive **只读拉取**可行；双向同步**不做**（CloudDocs / bird 无公开实现，冲突无法合并）
- 照片增量下载可走 icloudpd 轮询模式，注意节流

---

## 七、风险与边界

- **数据边界**：端到端加密项（钥匙串密码、开启 ADP 后的保护项）一律不在范围内
- **条款与风控**：非官方客户端访问违反 Apple 服务条款，存在锁号风险。
  一律**用副 Apple ID 测试**；仅本机自查用，不公开分发凭据相关实现
- **依赖风险**：路 A 依赖 pyicloud 追 Apple 的私有 Web API。**锁版本号 + 探针留作回归**，
  一旦 Apple 改动，探针能第一时间报出来
- **凭据存储**：keyring 或加密文件，禁明文落盘；与现有 QSettings 分开放
