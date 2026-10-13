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

冒烟已验（2026-10-13）：地区探测正确；两个 CalDAV host 都通（假账号返回 401，
预期内）；pyicloud 分支能到 Apple 的 SRP 端点并给出 "Invalid email/password"。

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
