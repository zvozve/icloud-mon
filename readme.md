# iCloudMon

Windows 上 iCloud Drive（Store 版）的同步状态监控器。

数据来源不是猜测，也不是私有 API，而是直接读 iCloud 自己的 SQLite 状态库
`iCloudDrive.db`，所以显示的每个计数都是真实值。

技术栈：Qt 5.15.2 + msvc2019 32bit + QML，第三方日志库 spdlog（源码随仓库走）。

---

## 一、功能

| 区域 | 内容 |
|---|---|
| 顶栏 | iCloud 进程运行状态（开关控制重启）、识别到的进程数、设置入口 |
| 状态卡 | 当前状态（空闲 / 同步中 / 上传中 / 下载中 / 双向传输 / 暂存中 / 上传处理中）+ 详细说明 |
| 上传面板 | 待上传个数与字节、已完成上传 |
| 下载面板 | 待下载个数与字节、待拉取数、已完成下载 |
| 云端分布 | 按 `pcs_state` 统计的文件状态分布（空文件 / 有本地实体 / 仅云端 / 目录） |
| 待处理项 | 明确列出待上传、待下载的文件名与大小 |
| 异常告警 | 上传错误码与个数、下载错误码与个数、容量超限 |
| 卡死检测 | 处于活动状态但活动指纹连续 N 秒无变化 → 红色横幅告警 |
| 打开目录 | 顶栏 📂 按钮，资源管理器打开 iCloud Drive 同步根目录 |
| 设置页 | 刷新间隔、卡死阈值、自启动开关、同步根目录选择、运行日志入口 |

### 卡死是怎么判的

「活动状态」+「活动指纹」两个条件同时成立才算异常。

指纹只由**真实计数**构成（待上传数、待下载数、待拉取数、字节数等），
**绝对不含时间戳或速率**。这一点很关键：如果把时间也算进指纹，
每一秒指纹都在变，卡死就永远检测不出来。

阈值默认 300 秒，可在设置页调整。

---

## 二、数据来源

iCloud Drive 的完整状态存在一个明文 SQLite 库里：

```
C:\Users\<用户>\AppData\Local\Packages\AppleInc.iCloud_nzyj5cx40ttqa\
    LocalCache\Local\Apple Inc\iCloudDrive\iCloudDrive.db
```

### 关键表与字段语义（已实测确认）

| 表 / 字段 | 含义 |
|---|---|
| `client_state.next_syncdown_rank` | 客户端已处理到的同步游标 |
| `server_state.next_syncdown_rank` | 服务端已下发的游标 |
| `unapplied_ranks` | 两游标之间未应用的条目数，为 0 表示追平 |
| `item_recursive_properties` | 待上传 / 待下载队列计数（空闲时全为 0） |
| `server_items_pending_fetches` | 待拉取条目，行数即待下载数 |
| `server_items.pcs_state` | 0=空文件 1=有本地实体 2=仅云端 4=目录 |
| `item_state` | 0=已同步，非 0=待处理 |
| `item_localsyncupstate` | 0=干净，非 0（含 4）=卡在进行中 |

**追平判据**：`rankClient == rankServer` 且 `unapplied == 0`。

### 怎么读这个库：必须先拷贝再只读打开

iCloud 正在运行时这个库被独占，直接打开会拿到脏数据或被锁。
所以每次采样都先把 **三个文件一起** 拷到临时目录，再只读打开：

```
iCloudDrive.db
iCloudDrive.db-wal      ← 漏了这个会读到旧数据
iCloudDrive.db-shm      ← 漏了这个 WAL 模式打不开
```

只拷贝 `.db` 是不够的，这是最容易踩的坑。

---

## 三、目录结构

```
icloud-mon/
├─ inc/            头文件
│  ├─ IcloudPaths.h       所有路径与常量的唯一来源
│  ├─ IcloudSnapshot.h    一次采样的完整数据快照
│  ├─ IcloudMonitor.h     采样与状态机
│  ├─ IcloudService.h     进程管理 / 重启 iCloud
│  ├─ AutoStart.h         开机自启动（注册表）
│  ├─ Log.h               spdlog 薄封装
│  └─ Bridge.h            QML 与 C++ 的桥
├─ src/            实现，与 inc/ 一一对应
├─ ui/             QML 界面
│  ├─ Theme.qml          深色主题单例（pragma Singleton）
│  ├─ Main.qml           主界面
│  ├─ StatusCard.qml     状态卡
│  ├─ QueuePanel.qml     上传 / 下载面板
│  ├─ PcsPanel.qml       云端分布
│  ├─ AlertBanner.qml    异常告警
│  └─ SettingsPage.qml   设置抽屉
├─ resources/      Qt 资源（qml 打包进 exe + qmldir）
├─ third_party/
│  └─ spdlog/      spdlog v1.14.1，仅 include/ + LICENSE
├─ build.bat       一键构建（vcvars32 + cmake + ninja + windeployqt）
└─ CMakeLists.txt
```

---

## 四、构建

环境：Qt 5.15.2 msvc2019 32bit、CMake 3.16+、Ninja、MSVC 2019。

```bat
build.bat
```

脚本内封装了四步：`vcvars32.bat` → `cmake` 配置 → `cmake --build` → `windeployqt`。

手动构建：

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars32.bat"
cmake -S . -B build -G Ninja ^
      -DCMAKE_PREFIX_PATH=C:/Container/System/Library/QT/5.15.2/msvc2019 ^
      -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### 构建期几个必须注意的点

| 坑 | 后果 | 处理 |
|---|---|---|
| `AUTOMOC` 只扫描 target sources 里的文件 | `inc/` 下头文件不列出来，moc 不触发，报 34 个 `staticMetaObject` / `qt_metacall` 未解析 | `add_executable` 显式列出全部 `inc/*.h` |
| spdlog 头文件数量极多 | MSVC 报 C1128 段数过多 | 加 `/bigobj` |
| `AUTORCC` 与 `qt5_add_resources` 重复处理 | 资源二次编译 | `CMAKE_AUTORCC OFF` |
| `windeployqt` 5.15 不认 `--32bit` | 报 Unknown option | 用 `--release`，它自动判定位数 |
| `pragma Singleton` 必须配套 `qmldir` | QML 加载失败 | `resources/qmldir` 内容为 `singleton Theme 1.0 Theme.qml` |
| `--qmldir=ui` 必须给 | 目录选择对话框 `QtQuick/Dialogs` 不会被部署，运行时才报缺模块 | `build.bat` 已带该参数 |

### QML 侧的限制

| 限制 | 说明 |
|---|---|
| `Rectangle` 没有 `borderColor` 属性 | 边框在 `border.color` 分组属性里；分组属性不能直接挂 Animation，需中介色属性 |
| Layout 管理的子项不能用 `anchors` 居中 | 会报 `Detected anchors on an item that is managed by a layout`，改用 `Layout.alignment` |

---

## 五、日志系统

基于 spdlog，按天切分，输出到 **exe 同级 `log\` 目录**：

```
build\Release\log\icloudmon_2026-10-09
```

| 项 | 值 |
|---|---|
| 切分时间 | 每天 23:59 换日 |
| 保留 | 最近 30 天 |
| 单文件上限 | 20 MB，超出再切 |
| 编码 | UTF-8（中文必须走 `toUtf8()`，用 `toStdString()` 会按 GBK 转成乱码） |
| 模式 | header-only，静态编译进 exe，无额外 DLL |

### 记录三类内容

| 前缀 | 类别 | 记什么 |
|---|---|---|
| `[RUN]` | 软件自身运行状态 | 启动 / 退出、Qt 版本、驱动就绪、monitor 生命周期 |
| `[SYNC]` | iCloud 同步变化 | 状态名变化（如 `空闲 → 上传中`）、队列计数变化 |
| `[ACTION]` | 操作记录 | 重启 iCloud、开关自启动、改阈值、打开日志目录等，全部带结果 |
| `[ANOMALY]` | 异常 | 卡死告警、恢复；数据库可读性翻转 |
| `[SQL]` | SQL 失败 | 语句与错误原文 |
| `[QT]` | Qt 内部消息 | Qt 自己的告警 / 错误，转发进来便于事后定位 |

Qt 的 `qWarning` / `qCritical` 通过 `qInstallMessageHandler` 全部转发进日志，
QML 加载失败这类只往 stderr 打的问题，关掉控制台后也能查到。

在设置页点「打开」可直接调资源管理器进这个目录。

---

## 六、同步根目录的取用

路径优先级，从外到内：

| 优先级 | 来源 | 用途 |
|---|---|---|
| 1 | 环境变量 `ICLOUD_DRIVE_ROOT` | 临时调试，可临时改指向而不动注册表 |
| 2 | 注册表 `HKCU\Software\zvozve\iCloudMon` 的 `driveRoot` | 用户在设置页选的路径，重启后仍有效 |
| 3 | 内置默认 `C:/Container/Cloud/iCloudDrive` | 兜底 |

设置页选目录时会做两项校验：目录必须存在、目录内应有 `.icloud` 隐藏文件
（iCloud Drive 的标志）。第二项只写日志不阻断 —— 有少量目录可能为空，
不该因此让用户选不了。

**为什么不用只读的上下文属性**：`main.cpp` 原本注入过一个 `DriveRoot`
上下文属性，那是**启动时的快照**，用户改完设置不会刷新。现已删除，
统一走 `Bridge.driveRoot` 属性（带 `NOTIFY driveRootChanged`）。

### 目录选择对话框的 API 细节

`QtQuick.Dialogs` 的 `FileDialog` 选目录用的是 **`selectFolder: true`**，
没有 `folderMode` 属性 —— `folderMode` 是 `Qt.labs.platform` 的 API，两者不同。
写错会在运行时报 `Cannot assign to non-existent property`。

---

## 七、历史基线存放位置

卡死检测需要跨进程比较，所以历史状态落盘。
位置在**用户 AppData**，不是 exe 同级：

```
C:\Users\<用户>\AppData\Roaming\zvozve\iCloudMon\icloud_state.jsonl
```

原因：exe 可能装在 Program Files 下只读，写不进去卡死检测就彻底失效。

---

## 八、已修复的关键坑

### `Driver not loaded`：QSqlQuery 没绑定连接

**症状**：`db.open()` 返回 `true`，但每个 `exec()` 都报
`Driver not loaded`，界面显示「数据库不可读」。

**根因**：`QSqlQuery` 的无参构造绑定的是**默认连接**
（`QSqlDatabase::database()` 返回的那个）。而本项目用的是**具名连接**
`icm_<时间戳>`，从未建过默认连接。所以查询压根没问到我们这条连接。

```cpp
QSqlQuery q;             // ✗ 绑定默认连接 -> Driver not loaded
QSqlQuery q(db);         // ✓ 必须把连接传进去
```

**排查代价**：这个症状极具误导性，先后误判为插件路径、DLL 位数、
线程归属、连接生命周期四类问题，全部排除后才定位到这里。

**容易漏的点**：声明形式稍变就会漏掉，`grep 'QSqlQuery q;'` 查不全。

| 写法 | 是否匹配简单 grep | 说明 |
|---|---|---|
| `QSqlQuery q;` | 匹配 | 单变量 |
| `QSqlQuery q2;` | **不匹配** | 名字含数字，`[a-zA-Z_]*` 覆盖不到 |
| `QSqlQuery qc, qs, qu;` | **不匹配** | 多变量声明，分号不在变量名后 |

正确的检查方式：

```bash
grep -nE '^\s*QSqlQuery ' src/IcloudMonitor.cpp   # 逐行看，别用窄模式
```

本轮就是先修了 10 处、剩这 2 处漏网，导致「修完还报同样的错」。

### 用一次性诊断代替猜测

错误文本分不出「驱动没装 / 连接没注册 / 连接没打开」三种情况，
所以在首次查询失败时打一条诊断，把连接真实状态摊开：

```
[SQL-DIAG] isValid=1 isOpen=1 drv=1 drvOpen=1
[SQL-DIAG] connName=icm_... dbName=file:...?mode=ro
[SQL-DIAG] 已注册连接=[...]
[SQL-DIAG] 可用驱动=[QSQLITE,...] 库路径=[...]
```

正是这条诊断证明连接完全正常，才把方向从「连接层」扭到「查询绑定层」。

### QML 属性名不能凭记忆写

本轮踩了两个，都只在**运行期**报错：

| 写法 | 实际 API | 报错 |
|---|---|---|
| `SequentialAnimation { when: ... }` | Animation 没有 `when`，只有 `running` | `Cannot assign to non-existent property "when"` |
| `Behavior { when: ... }` | Behavior 没有 `when`，只有 `enabled` | 同上 |
| `HoverHandler { }` 上取 `containsMouse` | HoverHandler 的属性是 `hovered` | `Unable to assign [undefined] to bool` |
| `FileDialog { folderMode: ... }` | `QtQuick.Dialogs` 用 `selectFolder: true` | `Cannot assign to non-existent property` |

核实方式（别猜，直接问 Qt 安装目录）：

```bash
grep -n 'Q_PROPERTY' $QT/include/QtQuick/5.15.2/QtQuick/private/qquickanimation_p.h
grep -n 'Q_PROPERTY' $QT/include/QtQuick/5.15.2/QtQuick/private/qquickbehavior_p.h
```

### QML 绑定错误会拖慢界面

`Repeater` 的 delegate 里写 `width: ListView.view.width` ——
delegate 不属于 ListView，`ListView.view` 是 `null`，每帧报错。
再加上 delegate 声明了 `required property` 之后 `index` 不再自动注入
（必须自己写 `required property int index`），错误条目会反复刷。

这些错误经 Qt 消息处理器 → spdlog → **同步写盘**，连续报错时就是
GUI 线程上的持续磁盘 I/O，直接表现为拖动窗口卡顿。

### Qt 消息必须限流

Qt 某些警告会高频重复。实测本机在剪贴板被其他进程占用（另一个应用
弹着模态框并持有剪贴板）时，`QWindowsClipboard` 会以约 50ms 间隔
反复重试并告警。Qt 的重试实现里带阻塞 `Sleep`，直接在 GUI 线程上停顿。

`main.cpp` 里对 Qt 消息做了**按内容 + 10 秒窗口**的限流。必须用时间
窗口，不能只比对上一条 —— 剪贴板那组是 `Retrying...` / `Unable to...`
**交替**出现的，相邻两条永远不同。

### 日志必须逐条落盘

原来 `flush_on(warn)`，info 级记录全留在缓冲区，进程被强杀
（任务管理器结束 / SIGTERM / 崩溃）时全部丢失 —— 而这恰恰是最需要
看日志的时候。已改为 `flush_on(info)`，本应用日志量很小，代价可忽略。

---

## 九、第三方与许可

| 组件 | 版本 | 许可 | 位置 |
|---|---|---|---|
| spdlog | v1.14.1 | MIT | `third_party/spdlog/`（含 LICENSE） |
| Qt | 5.15.2 | LGPL v3 / 商业 | 外部安装，不入库 |

iCloud Drive 的状态库由 Apple 创建，本项目**只读**，不修改、不上传。