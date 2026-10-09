# iCloudMon

Windows 上 iCloud Drive（Store 版）的同步状态监控器。

数据来源不是猜测，也不是私有 API，而是直接读 iCloud 自己的 SQLite 状态库
`iCloudDrive.db`，所以显示的每个计数都是真实值。

技术栈：Qt 5.15.2 + msvc2019 32bit + QML，第三方日志库 spdlog（源码随仓库走）。

---

## 一、功能

### 页面划分（左侧导航切页，仿 UWP NavigationView）

| 页面 | 回答的问题 | 内容 |
|---|---|---|
| **首页** | 现在到底什么状态？ | 异常横幅 + 状态卡 + 上传/下载/**删除** 三个队列 |
| **状态** | 具体是哪些文件在等？ | 云端状态分布 + 待处理项明细 + **回收站**（可恢复） |
| **设置** | 怎么配？ | 自启动、刷新间隔、卡死阈值、同步根目录、日志、系统通知 |

### 各区域明细

| 区域 | 内容 |
|---|---|
| 导航栏 | 左侧**通高**栏；☰ 可折叠成纯图标；设置固定在底部，与业务项用分隔线分开 |
| 命令栏 | 状态指示灯 + 状态名 + 版本、iCloud 开关与重启、打开 iCloud 目录 |
| 状态卡 | 当前状态（空闲/同步中/上传中/下载中/双向传输/暂存中/上传处理中）+ 游标 + 进程 |
| 上传面板 | 待上传个数与字节、已完成上传 |
| 下载面板 | 待下载个数与字节、待拉取数、已完成下载 |
| **删除面板** | 待删除个数（`needs_delete_doc_count`）、已完成=回收站项数 |
| 回收站 | 已删除但可恢复的项，带**原始路径**（`item_restore_path`） |
| 云端分布 | 按 `pcs_state` 统计的文件状态分布（空文件 / 有本地实体 / 仅云端 / 目录） |
| 待处理项 | 只列**真正**待处理的（有本地实体且状态非 0）；「仅云端」单独计数，不混进来 |
| 异常告警 | 上传错误码与个数、下载错误码与个数、容量超限 |
| 卡死检测 | 处于活动状态但活动指纹连续 N 秒无变化 → 红色横幅 + **Windows 系统通知** |
| 打开目录 | 命令栏 📂 按钮，资源管理器打开 iCloud Drive 同步根目录 |

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
| ↳ `needs_upload_count` | 待上传数 |
| ↳ `needs_download_count` | 待下载数 |
| ↳ **`needs_delete_doc_count`** | **待删除数** |
| ↳ `uploaded_count` / `downloaded_count` | 已完成上传 / 下载 |
| `local_items.item_restore_path` | **非空 = 该项已被删除、在回收站里**，值即原始路径（可恢复） |
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
├─ ui/             QML 界面（全部平铺, 不分子目录 —— 见下方说明）
│  ├─ Theme.qml          深色主题单例（pragma Singleton）
│  ├─ Main.qml           外壳: 左侧导航 + 命令栏 + 页面容器
│  ├─ NavIcon.qml        导航图标（图元绘制, 不依赖图标字体）
│  ├─ NavItem.qml        导航项（选中指示条 / 折叠态 / ToolTip）
│  ├─ OverviewPage.qml   首页: 异常横幅 + 状态卡 + 上传下载队列
│  ├─ StatusPage.qml     状态: 云端分布 + 待处理项明细
│  ├─ SettingsPage.qml   设置（独立页面）
│  ├─ StatusCard.qml     状态卡
│  ├─ QueuePanel.qml     上传 / 下载面板
│  ├─ PcsPanel.qml       云端分布
│  └─ AlertBanner.qml    异常告警
├─ resources/      Qt 资源（qml 打包进 exe + qmldir）
├─ third_party/
│  └─ spdlog/      spdlog v1.14.1，仅 include/ + LICENSE
├─ build.bat       一键构建（vcvars32 + cmake + ninja + windeployqt）
└─ CMakeLists.txt
```

---

### 界面结构（仿 UWP NavigationView）

```
┌──────────┬──────────────────────────────────────┐
│ ☰ 标题   │  命令栏: 状态点/版本   iCloud 开关 …  │
│ ──────── ├──────────────────────────────────────┤
│ ⌗ 首页   │                                      │
│ ▥ 状态   │      页面内容（StackLayout 切页）      │
│          │                                      │
│ ⚙ 设置   │                                      │
└──────────┴──────────────────────────────────────┘
```

页面划分与各区域明细见「一、功能」。

**几个刻意的决定：**

- **导航栏通高**，不是放在 `ApplicationWindow.header` 下面 —— UWP 的
  NavigationView 就是占满整个窗口高度。为此去掉了 `header`，
  原来的顶栏内容拆成「导航栏顶部标题」+「右侧命令栏」。
- **设置固定在最底部**，与业务导航项用分隔线分开 —— UWP 惯例。
- **设置改成独立页面**，删掉了原来的半透明遮罩、滑入/滑出动画和关闭按钮：
  既然由导航切页，就不再需要「弹出再收起」这层交互。
- **折叠态**只留图标并居中，文字淡出，用 ToolTip 补文字。
- 图标用 **`NavIcon.qml` 以图元绘制**（汉堡线 / 2×2 方块 / 柱状图 /
  带旋钮的滑条），**不用 Segoe MDL2 Assets 图标字体**。
  本机 `segmdl2.ttf` 和 `SegoeIcons.ttf` 都在，但字体码点记错会显示成方框，
  而几何图元不存在这种失败模式。

### QML 全部平铺在 `ui/` 下，不建子目录

QML 的**同目录类型解析**只认「导入文件所在目录」。把页面放进 `ui/pages/`
之后，`SettingsPage.qml` 里的 `StatusCard`、`QueuePanel` 就解析不到
（除非再补一份 `qmldir` 声明每个类型）。平铺最省事，也和现有结构一致。

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
| `[PERF]` | 自身性能 | 采样耗时超过 100ms 时记一条，含「复制+读库 / 历史 / 停滞检测」分项 |
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

### 拉起 Store 版 iCloud 必须用正确的 AUMID

`explorer.exe shell:AppsFolder/<AUMID>` 是拉起 Store 应用的唯一方式。

代码里原来写的是 `Microsoft.Store_8wekyb3d8bbwe!App` ——
那是**微软商店**的 AUMID，不是 iCloud 的。后果就是「重启 iCloud」
只能杀掉、拉不起来：explorer 去开商店，iCloud 一直不启动。

正确值从包安装目录的清单里取，**不要猜**：

```bash
ls -d "/c/Program Files/WindowsApps/AppleInc.iCloud"*/
grep -oE '<Application Id="[^"]+"' <包目录>/AppxManifest.xml
```

| 项 | 值 | 来源 |
|---|---|---|
| PackageFamilyName | `AppleInc.iCloud_nzyj5cx40ttqa` | 包目录名 = Name_版本_架构__PublisherId |
| Application Id | `iCloud` | AppxManifest 的 `<Application Id=...>` |
| AUMID | `AppleInc.iCloud_nzyj5cx40ttqa!iCloud` | `<PackageFamilyName>!<Application Id>` |

回退路径也从 `cmd /c start` 改成 `explorer.exe <lnk>` ——
少依赖 cmd，也避开中文系统上的编码问题。

### 进程探测不能用 tasklist

原来 `runningProcesses()` 是对 4 个进程名各起一次 `tasklist /FI ...`
并 `waitForFinished(4000)` **阻塞等待** —— 每 3 秒就在 GUI 线程上产生
4 次进程创建 + 4 次阻塞等待，合计 200～400ms 的主线程停顿。
这就是「拖动窗口卡顿」在量级上的主因，而且每 3 秒必发生一次。

现在改用 Win32 `CreateToolhelp32Snapshot` 一次读内核快照，不派生任何子进程，
耗时 1ms 量级：

```cpp
HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
if (Process32FirstW(snap, &pe)) { do { ... } while (Process32NextW(snap, &pe)); }
CloseHandle(snap);
```

进程名比较统一转小写（iCloud 各进程名大小写不完全一致），并按名字去重。

进程清单本身无需猜：用上面的 AUMID 拉起 iCloud 后 `tasklist` 一看即可，
实测与代码里配置的 4 个名字完全一致
（`iCloudDrive.exe` / `iCloudHome.exe` / `iCloudCKKS.exe` / `ApplePhotoStreams.exe`）。

### 无限循环动画 = 持续满帧重绘

`StatusCard` 的边框呼吸动画是 `loops: Animation.Infinite` +
`running: Bridge.anomalous`。而卡死是**持续状态**（本机实测停滞超 900 秒），
`anomalous` 长期为 true → 整个窗口无休止按 60fps 重绘 → 拖动必然卡。
已删除，边框改为静态变红。信息量不丢：异常已有顶部红色横幅表达。

### 告警会重复上报，UI 必须做去重

卡死这类异常是**每采样一次上报一次**（每 5 秒），不是只在出现时上报一次。
所以 `Bridge::onAnomaly` 里「是否新异常」的标志必须在赋值前先取；
弹窗也只在新异常 / 换种类时给，否则持续卡死状态下 Toast 动画会被每 5 秒重启。

### 测自己的卡顿：采样分项计时

采样全程跑在 GUI 线程上，超过 100ms 就会表现为界面 / 拖动卡顿。
`doSample()` 里记一条 `[PERF]`，含「复制+读库 / 历史 / 停滞检测」分项，
只在超阈值时写，避免平时刷屏。有它才能定位是哪一段慢，而不是靠感觉。

---

### `item_state` 不等于「待处理」—— 必须排除 `pcs_state=2`

这是最容易搞错的一处语义。

| 字段组合 | 真实含义 | 该怎么显示 |
|---|---|---|
| `item_state<>0` 且 `pcs_state<>2` | 有本地实体、状态非 0 | **待处理** |
| `item_state<>0` 且 `pcs_state=2` | **仅云端，本地没有实体** | **不算待处理**，是 iCloud Drive 的按需下载（Files On-Demand），属正常状态 |
| `pcs_state=2`（任意 item_state） | 仅云端 | 单独计数，标明「按需下载」 |

本机实测：按旧口径（只看 `item_state<>0`）会数出 **12 个「待处理项」**，
加上 `pcs_state<>2` 条件后只剩 **1 个**，另外 **139 项是仅云端**。
也就是说界面上原来挂了 11 个根本不存在的「待办」。

判据：按需下载的项**在本地磁盘上找不到**（实测 12 项里 11 项不存在），
而传输队列是空的、同步游标已追平 —— 三者一致说明「没有东西在传」，
那些计数就不可能是待处理任务。

### 删除队列：有「待删除」但没有「已完成删除」

三个队列是**天然对称**的 —— 都在 `item_recursive_properties` 表里：

| 队列 | 待处理 | 已完成 |
|---|---|---|
| 上传 | `needs_upload_count` | `uploaded_count` |
| 下载 | `needs_download_count` | `downloaded_count` |
| 删除 | `needs_delete_doc_count` | **不存在** |

这张表共 25 个字段，其中**没有** `deleted_count` 之类的已完成计数器，
也没有 `needs_delete_size`（所以删除队列没有「字节数」那一栏）。

所以删除队列的「已完成」侧改用**回收站项数**对应：
`local_items.item_restore_path` 非空 = 该本地项已被删除、躺在 iCloud 回收站里，
字段值就是**原始路径**（可据此恢复）。本机实测 11 项。

这同时也是删除维度上唯一能给出**明细**的真实数据，所以在状态页做了
「回收站」列表，显示当前名字 + 原路径 —— 光看名字认不出是什么，
原路径才能判断该不该恢复。

顺带：`local_items.item_use_icloud_trash` 本机 203 行全为 1，
是「启用 iCloud 回收站」的全局设置，不是逐项的队列状态，别拿它当队列用。

### 卡死告警要弹 Windows 系统通知

窗口最小化、或在别的虚拟桌面时，界面里的 Toast 根本看不到。
真正能提醒到人的只有系统通知。

- `QSystemTrayIcon::showMessage()` 是 Qt 的做法，**它属于 QtWidgets**
  （不是 QtGui），所以 `find_package` / `target_link_libraries` 要加 `Widgets`。
  部署代价为零 —— `windeployqt` 本来就已带 `Qt5Widgets.dll`
  （`QtQuick.Dialogs` 依赖 `QtQuick.PrivateWidgets`）。
- **Windows 上没有托盘图标就弹不出通知**（`Shell_NotifyIcon` 的 `NIF_INFO`
  前提是有托盘项），所以必须建托盘。
- 托盘图标用 `QPainter` 在运行时画（32×32），**不依赖外部资源文件** ——
  `QSystemTrayIcon` 没有图标时在部分 Windows 版本上托盘项不显示，
  连带 `showMessage` 也弹不出来。
- 刻意**不建右键菜单**：`QMenu` 是控件，只有 `QGuiApplication` 时创建控件
  是不受支持的。托盘只承担「显示通知」这一个职责。
- 通知只在**刚进入卡死**时弹一次（`Bridge::stallAlert` 专用信号），
  不复用 `notify` —— 后者在用户操作、其他异常时也会发，不该弹系统通知；
  而且卡死是持续状态、每 5 秒会上报一次，不区分就会一直弹。
- 用 `QSystemTrayIcon::isSystemTrayAvailable()` 判断，不可用时记一条警告，
  明确告知「只会记日志、不会弹通知」，而不是静默失效。

---

## 九、第三方与许可

| 组件 | 版本 | 许可 | 位置 |
|---|---|---|---|
| spdlog | v1.14.1 | MIT | `third_party/spdlog/`（含 LICENSE） |
| Qt | 5.15.2 | LGPL v3 / 商业 | 外部安装，不入库 |

iCloud Drive 的状态库由 Apple 创建，本项目**只读**，不修改、不上传。