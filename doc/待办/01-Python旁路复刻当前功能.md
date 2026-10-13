# 01 Python 旁路复刻当前功能

目标：数据层（取数 + 判定）迁到 Python 旁路进程，先做到**与现版本功能齐平**，
再在其上叠加新功能。界面、托盘、通知、自启动仍在 Qt 侧，本次不动。

状态：数据层骨架已落地并验证；Qt 侧接入待做。

## 已完成（数据层，分支 dev）

[x] sidecar/icloudmon/paths.py —— 路径与用户设置。注册表路径与 QSettings NativeFormat 对齐（HKCU\Software\zvozve\iCloudMon，键名里的 / 是子键）

[x] sidecar/icloudmon/snapshot.py —— 复制 db+wal+shm 到临时目录后只读查询；14 条查询与派生状态（stateName/stateDetail/isBusy）逐条对齐 IcloudMonitor.cpp

[x] sidecar/icloudmon/procs.py —— Toolhelp32 单次快照枚举，不派生 tasklist 子进程（避免已知的 200~400ms 主线程停顿）

[x] sidecar/icloudmon/stall.py —— 活动指纹 + 历史 jsonl + 卡死判定；指纹不含时间戳，观测期内不变时 last_change 退化为观测起点

[x] sidecar/icloudmon/service.py —— iCloud 启停：taskkill /F /IM 四进程；启动必须 explorer.exe shell:AppsFolder\<AUMID>（反斜杠）

[x] sidecar/serve.py —— NDJSON 常驻 IPC（start/stop/refresh/icloud/setting/history.reset/ping/quit）

[x] sidecar/probe.py —— 一次性采样探针

[x] 实测：probe 读到 clientVersion 3.12.11.6、已同步、client=3421 server=3421 待应用=0、仅云端 194、采样 45~96ms；serve 周期推送正常（interval=1，3.5s 内 4 帧）

## 待办

[ ] Qt 侧接入：QProcess 拉起 sidecar/serve.py，解析 NDJSON 写入 Bridge 属性，替换 IcloudMonitor + IcloudService 两条数据源

[ ] 三个 QML ListModel（pendingItems / localPendings / trashItems）改为从 payload 数组填充，绑定表达式保持不变

[ ] 异常分流复刻：anomalies[].kind == "卡死" 走系统通知，其余走窗口内 Toast；新种类或首次出现才弹，持续异常不重复弹

[ ] 四类同步分类移植（inc/IcloudCategories.h）：云盘走 iCloudDrive.db，相册走 local.db，密码走 keychain-2.db，书签走 ChromeBookmarks.db；含启动任务注册表 State 与手动覆盖项

[ ] 存储配额卡片：改用 Python plistlib 解析 iCloudWinPref.plist 的 StorageData 节点（自研 bplist 解析器在 C++ 侧没调通，Python 侧直接绕过），验证通过后接首页

[ ] 双轨验证：同一时刻 probe.py 的字段值与 C++ 版界面逐项比对（至少覆盖 stateName/upCount/dlCount/delCount/rankText/cloudOnlyCount）

[ ] 历史基线一致性核对：确认 C++ 与旁路写的是同一个 icloud_state.jsonl（%APPDATA%\zvozve\iCloudMon），避免两份历史互相干扰

[ ] 日志：旁路目前只出 JSON，需要落一份滚动日志，并保留 Qt 侧 spdlog 的关键事件（启动/状态变化/异常）

[ ] 内嵌运行时打包：python-3.x-embed-amd64 + pip install --target sidecar/runtime，随 Enigma Virtual Box 打进单文件 exe；加 .gitignore

[ ] 打包验证：单文件 exe 里 serve.py 能被拉起、免装 Python、无控制台窗口

[ ] 旧路径清理：确认 Qt 侧不再调用 C++ 数据源后，再决定 IcloudMonitor.cpp / IcloudService.cpp 是删除还是保留为参考（保留期间标记 deprecated）

## 决策记录

完整评估见 `doc/技术选型评估.md`（含 Electron + Python 的逐项对比）。结论：

采用方案 A 的理由：认证与网页 API 的复杂度放在 Python 侧（pyicloud 现成），Apple 改端点时只改脚本不重打包；用户侧零安装（内嵌运行时）。

不转 Electron 的理由：常驻内存 5~10 倍（20~60MB → 150~300MB）、包体 3~5 倍（27MB → 80MB+）、失去 Enigma VB 单文件绿色分发的等价物、杀软误报与签名压力上升、构建链从 2 套变 3 套；且 **Electron 43 是最后一个提供 win32-ia32 的版本，v44 起只发 64 位、v43 于 2027-01 EOL** —— 本项目是 32 位构建，转过去等于一年后无安全更新。

更本质：备忘录/待办/照片这些能力属于**数据层语言问题**，与界面框架无关，换 Electron 一样拿不到。

不采用纯 Rust 重写的理由：需要自己实现 SRP-6a + GSA + 2FA，工程量最大，等旁路验证完协议面之后再评估（届时可参照 icloudpd-rs）。

分工边界：数据层 Python、系统集成 C++（Qt）、界面 QML。采样实测 45~96ms，远未触及"需要 C++ 写数据层"的门槛（>200ms）。
