# 01 Python 旁路复刻当前功能

目标：数据层（取数 + 判定）迁到 Python 旁路进程，先做到**与现版本功能齐平**，
再在其上叠加新功能。界面、托盘、通知、自启动仍在 Qt 侧，本次不动。

状态：数据层骨架已落地并验证；Qt 侧接入已完成并实机验证（2026-10-13）。

## 已完成（数据层，分支 dev）

[x] sidecar/icloudmon/paths.py —— 路径与用户设置。注册表路径与 QSettings NativeFormat 对齐（HKCU\Software\zvozve\iCloudMon，键名里的 / 是子键）

[x] sidecar/icloudmon/snapshot.py —— 复制 db+wal+shm 到临时目录后只读查询；14 条查询与派生状态（stateName/stateDetail/isBusy）逐条对齐 IcloudMonitor.cpp

[x] sidecar/icloudmon/procs.py —— Toolhelp32 单次快照枚举，不派生 tasklist 子进程（避免已知的 200~400ms 主线程停顿）

[x] sidecar/icloudmon/stall.py —— 活动指纹 + 历史 jsonl + 卡死判定；指纹不含时间戳，观测期内不变时 last_change 退化为观测起点

[x] sidecar/serve.py —— NDJSON 常驻 IPC（start/stop/refresh/setting/history.reset/ping/quit）

[x] sidecar/probe.py —— 一次性采样探针

[x] 实测：probe 读到 clientVersion 3.12.11.6、已同步、client=3421 server=3421 待应用=0、仅云端 194、采样 45~96ms；serve 周期推送正常（interval=1，3.5s 内 4 帧）

[x] 职责归位：iCloud 启停**暂留 C++**（原 sidecar/icloudmon/service.py 已删除）—— 两侧都是调同一个 taskkill、效果打平，而 C++ 侧已实现且踩过坑（taskkill 128 视为成功、AppsFolder 反斜杠），还能直接驱动 UI 的 busy/progress 文案。旁路收到 `icloud` 命令明确报错，避免两处各写一份

## 已完成（Qt 侧接入，2026-10-13）

[x] src/SidecarClient.{h,cpp} —— QProcess 拉起 serve.py、NDJSON 收发、心跳 ping + 看门狗（max(3*间隔,20)s）、指数退避重拉（1s→30s 封顶）、UTF-8 环境强制（PYTHONUTF8/PYTHONIOENCODING）、退出时 quit → stdin EOF → terminate → kill 三级礼让

[x] inc/Bridge.h / src/Bridge.cpp —— 数据源从 IcloudMonitor 整体切到 SidecarClient；全部 Q_PROPERTY 改为读 payload（jint/jbool/jstr 三个辅助函数）；三个 ListModel 由 payload 数组填充，QML 绑定表达式零改动；异常分流复刻（"卡死"→系统通知，其余→Toast，仅首现/新种类弹）；离线时清空 payload 并合成一帧"监控不可用"，**不沿用旧数据**

[x] 过渡桥 snapshotForCategories() —— 四类同步还没迁 Python，先把 payload 里的原始计数反填成最小 Snapshot 喂给 IcloudCategories；迁移完成后整段删除（代码里有明确标注）

[x] src/IcloudSnapshot.cpp 独立成文件 —— stateName/stateDetail/isBusy 三个纯数据派生方法原来住在 IcloudMonitor.cpp，下线后 IcloudCategories 还要用，链接期缺符号才暴露"它们与 monitor 无关"；摘出来让 Snapshot 自包含

[x] CMakeLists 切换 —— IcloudMonitor.cpp/.h 移出构建（文件保留作参考），SidecarClient + IcloudSnapshot 加入

[x] m_wantSampling 闸门 —— 用户按过"停止"后旁路崩溃重拉不许自作主张续采

[x] m_procDead 闸门 —— 进程死后 stdout 残留帧不认，防止"监控不可用"刚切过去又被一帧死后数据闪回旧状态

[x] 日志标签分层 —— SidecarClient 记 [SIDECAR]（传输层事件），Bridge 记 [BRIDGE]（界面侧动作）；两处曾用同一文案打"离线"，日志里像重复调用，白查一轮

[x] 实机验证（2026-10-13，win 真机）：启动 → [SIDECAR] 已启动 → [BRIDGE] 在线；kill 旁路 → 0.2s 内离线（两层日志各一条）→ 1.35s 自动重拉恢复；强杀主程序 → 旁路随 stdin EOF 自退出，无孤儿；界面截图与 probe.py 逐字段对上（已同步 / 删除 1 项 / 仅云端 194 项 260.2 MB = 272813448 B / 待处理 0+1 孤立）

## 分工（一句话）

Python 采集（取数 + 判定 + 将来的网页 API）、C++ 客户端控制（iCloud 启停 + 桌面外壳 + 旁路生命周期 + 桥接）、QML 界面。

**选边原则（用户明确）：谁做得更好就用谁 —— 更准、更快、更省事的一侧胜出，不搞语言教条；唯一红线是同一件事只允许一处实现。** 判据与可迁条件见 `doc/架构边界与风险控制.md`。

## 待办

[ ] 四类同步分类移植（inc/IcloudCategories.h）：云盘走 iCloudDrive.db，相册走 local.db，密码走 keychain-2.db，书签走 ChromeBookmarks.db；含启动任务注册表 State 与手动覆盖项。**移植完删掉过渡桥 snapshotForCategories()**

[ ] 存储配额卡片：改用 Python plistlib 解析 iCloudWinPref.plist 的 StorageData 节点（自研 bplist 解析器在 C++ 侧没调通，Python 侧直接绕过），验证通过后接首页

[ ] 双轨验证：同一时刻 probe.py 的字段值与 C++ 版界面逐项比对（至少覆盖 stateName/upCount/dlCount/delCount/rankText/cloudOnlyCount）。2026-10-13 首页已对上一轮（见上"实机验证"），其余页面待逐页核对

[ ] 历史基线一致性核对：确认 C++ 与旁路写的是同一个 icloud_state.jsonl（%APPDATA%\zvozve\iCloudMon），避免两份历史互相干扰

[ ] 日志：旁路目前只出 JSON，需要落一份滚动日志，并保留 Qt 侧 spdlog 的关键事件（启动/状态变化/异常）

[ ] 内嵌运行时打包：python-3.x-embed-amd64 + pip install --target sidecar/runtime，随 Enigma Virtual Box 打进单文件 exe；加 .gitignore

[ ] 打包验证：单文件 exe 里 serve.py 能被拉起、免装 Python、无控制台窗口

[ ] 旁路离线态 ✅（已实现：心跳 + 看门狗，离线清 payload 合成"监控不可用"帧）；剩余：验收标准里的"3 个采样周期内离线"量化核对

[ ] 旁路进程生命周期 ✅（已实现并实测：quit → stdin EOF → terminate → kill；强杀主程序旁路自退出）；剩余：8 小时长跑无句柄泄漏

[ ] NDJSON 协议加版本字段，主程序遇到不认识的版本直接报错而非猜

[ ] 日志统一：旁路日志与 spdlog 落同一目录、同一时间戳格式、带 pid，便于跨进程对齐

[ ] 切换纪律：IcloudMonitor 已移出构建 ✅；IcloudService 保留（客户端控制归 C++）。剩余：确认稳定后删除 IcloudMonitor.cpp/.h 与过渡桥

[ ] 验收：连续 8 小时无僵尸进程/句柄泄漏；手动 kill 旁路 → 3 个周期内进离线态 ✅（实测 0.2s）；重启旁路自动恢复 ✅（实测 1.35s）；主程序退出后旁路随之消失 ✅（实测 stdin EOF 自退出）

[ ] 确认 32 位到底是不是需求（当前 32 位很可能只是手上是 msvc2019 32 位 kit）；若不是，用 MaintenanceTool 补装 `msvc2019_64`（MSVC 64 位编译器本机已具备：VS2019 Enterprise/BuildTools 的 Hostx64/x64），CMake 换 `-A x64` 重配，Enigma VB 支持 x64

[ ] 旧路径清理：IcloudMonitor.cpp/.h 已移出构建（文件保留作参考，CMake 里有注释）✅；IcloudService **保留参与构建**（客户端控制归 C++）。旁路稳定运行一段时间后删除 IcloudMonitor.cpp/.h

## 决策记录

完整评估见 `doc/技术选型评估.md`（含 Electron + Python 的逐项对比）。结论：

采用方案 A 的理由：认证与网页 API 的复杂度放在 Python 侧（pyicloud 现成），Apple 改端点时只改脚本不重打包；用户侧零安装（内嵌运行时）。

不转 Electron 的理由（2026-10-13 修正）：原先把"32 位断供"当第一理由，**该条已作废** —— 32 位是现状不是需求，Electron 与 Qt 都能上 64 位。剩下的理由是"代价大小"而非"能不能做"：常驻内存 5~10 倍（20~60MB → 150~300MB）、包体 3~5 倍（27MB → 80MB+）、失去 Enigma VB 单文件绿色分发的等价物、构建链从 2 套变 3 套。若目标改成"更好的 UI 开发体验 / 未来跨平台统一 UI"，Electron 可行，只是要接受上述代价。

更本质：备忘录/待办/照片这些能力属于**数据层语言问题**，与界面框架无关，换 Electron 一样拿不到。

不采用纯 Rust 重写的理由：需要自己实现 SRP-6a + GSA + 2FA，工程量最大，等旁路验证完协议面之后再评估（届时可参照 icloudpd-rs）。

分工边界：数据层 Python、系统集成 C++（Qt）、界面 QML。采样实测 45~96ms，远未触及"需要 C++ 写数据层"的门槛（>200ms）。
