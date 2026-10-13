# sidecar/ —— Python 旁路数据层（方案 A）

iCloudMon 的**取数与判定**从 C++ 迁到 Python 旁路进程，Qt/QML 侧只做显示。
目的不是"用 Python 重写界面"，而是把**协议/数据源的复杂度**放到能快速迭代的一侧：

| | C++ 直连（现状） | Python 旁路（方案 A） |
|---|---|---|
| 认证/网页 API 能力 | 需要自己实现 SRP+GSA+2FA，成本极高 | pyicloud 现成，备忘录/待办/照片即刻可用 |
| Apple 改端点 | 重新编译 + 重打包 | 改脚本，主程序不动 |
| 界面 | QML（不变） | QML（不变） |
| 依赖 | 0 | 内嵌 embeddable Python（用户无感） |

## 边界（别越界）

- 旁路**只做采集与判定**：不建窗口、不做托盘、不弹通知、**不做 iCloud 启停**。
  启停属于"客户端控制"，归 C++ 侧的 `IcloudService`（已实现且踩过坑）。
- 通知/托盘/自启动仍在 Qt 侧。
- 单一真相源仍在主进程：旁路输出的字段名与 `inc/Bridge.h` 的 `Q_PROPERTY` 同名，
  QML 绑定表达式不需要改。

分工一句话：**Python 采集、C++ 客户端控制、QML 界面。**

## 目录

```
sidecar/
  serve.py              常驻进程：NDJSON IPC 服务端
  probe.py              一次性采样，验证数值用
  probe_reminders.py    待办取数探针（只读）：CalDAV / pyicloud 两条路各探一次
  requirements-reminders.txt   待办取数依赖（锁版本；装进 runtime/）
  tests/
    test_no_retry.py    回归：证明认证路径不会自动重试（离线桩，不联网）
  icloudmon/
    paths.py            路径与用户设置（注册表，与 QSettings NativeFormat 对齐）
    snapshot.py         复制 db+wal+shm → 只读查询 → Snapshot（14 条查询 + 派生状态）
    bridge.py           Snapshot → 与 Bridge.h 属性同名的扁平 JSON
    procs.py            iCloud 进程检测（Toolhelp32 单次快照）
    stall.py            活动指纹 + 历史 jsonl + 卡死判定
  runtime/              venv，依赖装这里；不进版本库
```

## IPC 协议

一行一条 JSON（NDJSON），UTF-8，stdout 只出 JSON，报错也走 JSON（不写 stderr）。

**命令（stdin）**

```
{"id":1,"cmd":"start","interval":5}      启动周期采样
{"id":2,"cmd":"stop"}                    停止周期采样（停止前会补推一帧）
{"id":3,"cmd":"refresh"}                 立即采样，结果走 reply
{"id":5,"cmd":"setting.get","key":"driveRoot"}
{"id":6,"cmd":"setting.set","key":"ui/closeToTray","value":true}
{"id":7,"cmd":"history.reset"}           清空卡死检测基线
{"id":8,"cmd":"ping"}
{"id":9,"cmd":"quit"}
```

注意没有 `icloud` 命令——启停由 C++ 负责（发过去会明确报错）。

**事件（stdout）**

```
{"evt":"ready","data":{pid,driveRoot,dbFile,refreshSec,stallSec}}
{"evt":"snapshot","data":{...}}          周期推送，无 id
{"evt":"reply","id":n,"data":{...}}      命令响应
{"evt":"error","data":{message}}         采样/命令失败
{"evt":"bye","data":{pid}}
```

`data` 字段与 `Bridge.h` 属性一一对应；`data.anomalies` 是异常明细数组
（`{kind,title,detail}`，顺序与 `IcloudMonitor::doSample` 相同），Qt 侧据此决定
"卡死走系统通知、其余走窗口内 Toast"。

## 本地验证

```bash
python sidecar/probe.py            # 一次采样，打印关键字段
python sidecar/probe.py --full     # 完整 payload
python sidecar/probe.py --watch 5  # 连续采样
python sidecar/serve.py            # 手动喂 NDJSON 命令
```

实测（本机，2026-10-13）：采样 45~96ms（含复制 db+wal+shm），
`stateName=已同步`、`client=3421 server=3421 待应用=0`、`cloudOnly=194/仅云端`。

## 待办（Reminders）取数探针

`probe_reminders.py` 只读，回答"这个账号能不能拿到提醒事项、走哪条路"：

```bash
# 重建运行时（首次）
<python 3.10+> -m venv sidecar/runtime
sidecar/runtime/Scripts/python.exe -m pip install -i https://pypi.org/simple \
    -r sidecar/requirements-reminders.txt

# 路 B：CalDAV + Apple App 专用密码（零 2FA、口令可吊销；先试这个）
sidecar/runtime/Scripts/python.exe sidecar/probe_reminders.py caldav   --user you@icloud.com
# 路 A：pyicloud 原生登录（SRP-6a + 2FA）+ CloudKit
sidecar/runtime/Scripts/python.exe sidecar/probe_reminders.py cloudkit --user you@icloud.com
```

口令走 `getpass`（不回显），只打印掩码；地区默认从本机 `iCloudWinPref.plist`
自动判断（本机已判为**云上贵州**）。**不写任何数据。**

### 凭据与风控（硬约束，改动前先读）

| 项 | 事实 / 约束 |
|---|---|
| 口令去向 | **不落盘、不进日志、不走命令行**。SRP 只把 `sha256(口令)` 派生的证明发给 Apple，明文不出本机；脚本只打印掩码 |
| 为什么没有 `--password` | 命令行会进 PowerShell 历史（PSReadLine）和进程列表（WMI 可查），等于把口令写进本机日志。要非交互就用环境变量，**用完即删**：`Remove-Item Env:ICLOUDMON_PASSWORD` |
| 认证次数 | **单次**。失败立即退出，**绝不自动重试** —— 无论毫秒级循环还是几十上百次 |
| 为什么会锁号 | Apple 的失败计数按**账号**累加，连续重试 = 自己把号锁掉。失败后先看错误（口令 / 地区 / 网络），改对了再跑**一次** |
| 哪些动作消耗次数 | 只有两处：路 B 一次 host 登录（**默认单 host**）、路 A 一次 SRP 登录。`--probe-both-hosts` 会把路 B 变成最多两次，仅在怀疑地区判断错时用 |
| 会话缓存 `.sessions/` | 存的是**等价凭据**（`session_id` + `trust_token`，数周内可免 2FA），**不是**口令。不进版本库；`--clear-sessions` 即"退出登录" |
| 库内有无自动重试 | 已核实：pyicloud `_authenticate` / `_srp_authentication` 单次无循环；`PCS_MAX_RETRIES=10` 只用于照片服务设备授权轮询，与登录/待办无关；caldav 的限流重试（`rate_limit_handle`）**默认关闭**；CloudKit 遇 `THROTTLED` 是**抛异常**（带 Retry-After）而非自动重试 |
| ⚠ 别让重启变成登录 | C++ 侧对旁路进程有**自动重启退避**（1s→30s 封顶，`SidecarClient::scheduleRestart`）。认证**绝不能放在进程启动路径上** —— 否则进程每被拉起一次就是一次登录尝试。待办上线后必须：优先用会话 token **静默复用**；token 失效就标记"需重新登录"并**停止**，等用户手动操作，而不是随重启自动重登 |

离线验证（2026-10-13，用桩模块冒充 caldav / pyicloud，不联网、不碰 Apple 端点）：

```
sidecar/runtime/Scripts/python.exe sidecar/tests/test_no_retry.py

[OK ] caldav 国内 默认                       凭据提交 1 次 (期望 1)
[OK ] caldav 国际 默认                       凭据提交 1 次 (期望 1)
[OK ] caldav 地区未知 默认                   凭据提交 1 次 (期望 1)
[OK ] caldav 国内 --probe-both-hosts         凭据提交 2 次 (期望 2)
[OK ] cloudkit 默认                          凭据提交 1 次 (期望 1)
```

这是**回归测试**，不是一次性验证：任何人往认证路径里加了重试循环，它立刻红。

冒烟已验（2026-10-13）：地区探测正确；两个 CalDAV host 都通（假账号返回 401，
预期内）；pyicloud 分支能到 Apple 的 SRP 端点并给出 "Invalid email/password"。

**未来 Qt 侧登录表单（若走路 A）必须遵守同一条**：一次点击 = 一次提交，失败只给
提示、**不自动重试、不退避重试**；要再试必须由用户再点一次。

## 打包（用户零安装）

内嵌 `python-3.x-embed-amd64.zip` + `pip install --target` 安装依赖到
`sidecar/runtime/`，整个目录随 Enigma Virtual Box 打进单文件 exe，
用户拿到的仍是双击即用的绿色版，不需要装 Python、不进 PATH。
该目录不进版本库（见 `.gitignore`）。

## 注意

- 核心数据层**只用标准库**（sqlite3/json/ctypes/winreg/subprocess），
  只有新增的网页 API 功能才需要 `pyicloud` 等第三方包。
- 用户设置的注册表路径必须与 Qt 的 `QSettings` 一致：
  `HKCU\Software\iCloudMon\iCloudMon`，键名里的 `/` 在注册表里是子键。
- 卡死检测的历史文件位置必须与 C++ 版一致，否则两边基线不同步：
  `%APPDATA%\iCloudMon\iCloudMon\icloud_state.jsonl`。
