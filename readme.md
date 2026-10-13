# iCloudMon

Windows 上 iCloud Drive（Store 版）的同步状态监控器。

数据来源不是猜测，也不是私有 API，而是直接读 iCloud 自己的 SQLite 状态库
`iCloudDrive.db`，所以显示的每个计数都是真实值。

技术栈：Qt 6.8.3 + msvc2022 64bit + QML；数据层是内嵌的 Python 旁路
（纯标准库，随单文件 exe 一起打包，用户无需装 Python）；第三方日志库 spdlog
（源码随仓库走）。

> 工具链变更（1.0.5）：1.0.4 及以前是 **Qt 5.15.2 msvc2019 32bit**。现在的
> 64 位套件与公司项目仍在用的 Qt5 32 位套件**并存**于
> `C:/Container/System/Library/QT/` 下的不同子目录，互不影响；旧的 32 位
> 构建目录保留在 `build32/`。

---

## 一、功能

### 页面划分（左侧导航切页，仿 UWP NavigationView）

| 页面 | 回答的问题 | 内容 |
|---|---|---|
| **首页** | 四类同步各自开着吗？ | 只有四张类别卡片（启用状态 + 摘要），不放任何细节 |
| **云盘** | 云盘现在在干什么？ | 异常横幅 + 状态卡 + 上传/下载/删除三队列 + 云端分布 + 待处理项 + 回收站 |
| **相册** | 相册有什么数据？ | 图库/Asset/缩略图实有数 + 启用状态来源 |
| **密码** | 钥匙串里有多少？ | 网站密码/通用密码/密钥条数 + 启用状态来源 |
| **书签** | 书签同步到哪了？ | 客户端/服务端/同步记录/待删记录数 + 启用状态来源 |
| **设置** | 怎么配？ | 类别启用、侧边栏顺序、默认启动页、自启动、刷新间隔、卡死阈值、同步根目录、日志、系统通知 |

侧边栏顺序：**首页固定最前、设置固定最后**（不可调），中间四类可用 ↑↓ 调整，
顺序存进 `QSettings`（`HKCU\Software\iCloudMon\iCloudMon` 的 `nav/order`）。
默认启动页同样持久化（`nav/defaultPage`），只在启动时取一次 ——
不做成绑定，否则用户一改设置当前页就被拽走。

### ⚠ 关键：四类的启用状态为什么必须手动指定

iCloud 把每项服务的开关存在 `Settings/settings.dat` 里，那是 UWP 的设置
hive，**被 iCloudHome 独占锁定**（复制和读取都失败，`Device or resource busy`），
拿不到权威值。

退而求其次试了注册表的 UWP 后台任务 `StartupTask\State`（2=启用 0=未启用），
拿 iCloud 设置界面核对：

| 类别 | StartupTask State | iCloud 界面 | 是否吻合 |
|---|---|---|---|
| 云盘 | 2 | On | ✅ |
| 相册 | 0 | Off | ✅ |
| 密码 | 0 | On | ❌ |
| 书签 | 无此项 | On | ❌ |

**只有云盘和相册吻合。** 所以本程序把自动检测结果当作**参考值**显示
（页面上写明依据），最终生效值由用户在设置页手动指定，点「自动」可回退。
宁可让用户设一次，也不显示一个错的「未启用」。

### 各区域明细

| 区域 | 内容 |
|---|---|
| 导航栏 | 左侧**通高**栏；☰ 可折叠成纯图标；设置固定在底部，与业务项用分隔线分开；顶部为 **iCloud 标志 + 标题** |
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
| 打开目录 | 命令栏「打开目录」按钮（文件夹图标），资源管理器打开 iCloud Drive 同步根目录 |

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
│  ├─ IcloudPaths.h       所有路径与常量的唯一来源（含四类各自的数据源）
│  ├─ IcloudSnapshot.h    一次采样的完整数据快照
│  ├─ IcloudCategories.h  四类同步（云盘/相册/密码/书签）状态读取
│  ├─ SidecarClient.h     拉起/监控 Python 旁路进程, 解析它的 NDJSON 输出
│  ├─ SystemNotifier.h    专属 STA 线程发 WinRT 系统通知（见第八节）
│  ├─ IcloudService.h     进程管理 / 重启 iCloud
│  ├─ AutoStart.h         开机自启动（注册表）
│  ├─ Log.h               spdlog 薄封装
│  └─ Bridge.h            QML 与 C++ 的桥，数据全部来自旁路的 payload
├─ src/            实现，与 inc/ 一一对应
│  └─ IcloudMonitor.cpp   ★ 已下线: C++ 直读 db 的旧实现, 已被 Python 旁路取代。
│                           文件留作参考, 不参与构建, 确认旁路稳定后可删。
├─ ui/             QML 界面（全部平铺, 不分子目录 —— 见下方说明）
│  ├─ Theme.qml          深色主题单例（pragma Singleton）
│  ├─ Main.qml           外壳: 左侧导航(6 页) + 命令栏 + StackLayout
│  ├─ NavIcon.qml        导航图标（图元绘制, 不依赖图标字体; 含 folder/file/upload/download/trash 图元）
│  ├─ NavItem.qml        导航项（选中指示条 / 折叠态 / ToolTip）
│  ├─ AppSwitch.qml      统一开关（覆盖 Switch.indicator, 避免 Fusion 默认样式叠加）
│  ├─ AppButton.qml      统一按钮（ghost / primary / danger 三种变体）
│  ├─ AppSlider.qml      统一滑块（修复 track 不显示的 implicitHeight 问题）
│  ├─ HomePage.qml       首页: 四类同步卡片
│  ├─ DrivePage.qml      云盘: 横幅 + 状态卡 + 三队列 + 分布 + 待处理 + 回收站
│  ├─ CategoryPage.qml   相册/密码/书签通用详情页（数据驱动, 不写死类别）
│  ├─ SettingsPage.qml   设置（独立页面）
│  ├─ StatusCard.qml     状态卡
│  ├─ QueuePanel.qml     上传 / 下载 / 删除 队列面板
│  ├─ PcsPanel.qml       云端分布
│  └─ AlertBanner.qml    异常告警
├─ resources/      Qt 资源（qml 打包进 exe + qmldir）
│  ├─ icloud.svg        iCloud 品牌图标（应用/托盘/导航栏共用, 见下方商标说明）
├─ sidecar/        Python 旁路（数据层）：serve.py + icloudmon/ 包
│  ├─ runtime/         开发期用的 venv（不进库, gitignore）
│  └─ tests/           离线回归测试（如「认证不重试」）
├─ packaging/      打包与发布
│  ├─ package.py       一键出单文件 exe（编译→部署→内嵌旁路→打包）
│  ├─ gen_evb.py       生成 Enigma Virtual Box 工程
│  ├─ release.py       走 GitHub REST API 发 Release
│  └─ RELEASE_NOTES_*.md
├─ third_party/
│  └─ spdlog/      spdlog v1.14.1，仅 include/ + LICENSE
├─ build.bat       一键构建（vcvars64 + cmake + windeployqt）
├─ build-tools/    打包用的下载件（python-embed，不进库, gitignore）
├─ build/          64 位构建目录（不进库）；build32/ 是切 64 位前的旧 32 位目录
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
  带旋钮的滑条 / 文件夹 / 文件 / 上传 / 下载 / 回收站），**不用 Segoe MDL2
  Assets 图标字体**。本机 `segmdl2.ttf` 和 `SegoeIcons.ttf` 都在，但字体码点
  记错会显示成方框，而几何图元不存在这种失败模式。原先散落在各页的彩色
  emoji（📂📁📄⌫）已全部替换为 `NavIcon` 图元 —— 单色矢量体系里混进彩色
  emoji 就是「打开云盘图标有点突兀」的根源。
- **统一控件**：开关 / 按钮 / 滑块不再各页手写，而是抽到
  `AppSwitch.qml` / `AppButton.qml` / `AppSlider.qml` 三个组件
  （经 `resources/icloudmon.qrc` 打包进 exe），全站复用同一套尺寸与配色
  （`Theme.btnH` / `Theme.switch*` / `Theme.slider*`）。此前每页各写一版，
  尺寸参差、配色不统一；更隐蔽的是设置页开关把 `parent.checked` 写成了外层
  Rectangle（而非 Switch），导致圆点永远不移动、开/关只有颜色变。统一组件后
  从根上消除这类不一致。
- **导航栏顶部品牌图标**：原来是纯文字标题，改用 `icloud.svg` 的彩色渐变
  云朵（左侧 24×24），与应用窗口图标、托盘图标同源，保证三处视觉一致。

### QML 全部平铺在 `ui/` 下，不建子目录

QML 的**同目录类型解析**只认「导入文件所在目录」。把页面放进 `ui/pages/`
之后，`SettingsPage.qml` 里的 `StatusCard`、`QueuePanel` 就解析不到
（除非再补一份 `qmldir` 声明每个类型）。平铺最省事，也和现有结构一致。

---

## 四、构建

环境：Qt 6.8.3 msvc2022_64bit、CMake 3.16+、MSVC 2022（Build Tools 即可）。

```bat
build.bat
```

脚本内封装了四步：`vcvars64.bat` → `cmake` 配置 → `cmake --build` → `windeployqt`。

手动构建：

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
      -DCMAKE_PREFIX_PATH=C:/Container/System/Library/QT/6.8.3/msvc2022_64
cmake --build build --config Release
```

出**单文件绿色版**（编译 → 部署 Qt → 内嵌 Python 旁路 → 压成一个 exe）：

```bat
python packaging\package.py --version 1.0.6
```

### 构建期几个必须注意的点

| 坑 | 后果 | 处理 |
|---|---|---|
| `AUTOMOC` 只扫描 target sources 里的文件 | `inc/` 下头文件不列出来，moc 不触发，报 34 个 `staticMetaObject` / `qt_metacall` 未解析 | `add_executable` 显式列出全部 `inc/*.h` |
| spdlog 头文件数量极多 | MSVC 报 C1128 段数过多 | 加 `/bigobj` |
| `qt5_add_resources` 不跟踪 QML 改动 | 改了 `ui/*.qml` / `resources/*.svg` 界面，重新构建后跑出来还是旧版 —— 该命令只把 `.qrc` 当依赖，不查它引用的文件 | 删掉 `qt5_add_resources` 块，启用 `CMAKE_AUTORCC ON`：AUTORCC 会跟踪 qrc 内每个引用文件，任何 QML / svg 改动都触发重打包（用唯一探针串进 exe 可验证） |
| 生成器不能中途换 | 同一个 `build/` 目录先用 Ninja 配置过，再换 "Visual Studio 17 2022" 会被 CMake 拒绝 | 固定用 VS 生成器（与 CMakeCache 一致）；要换就删掉 `build/` |
| `windeployqt` 必须用**同套件**的 | 拿 Qt5 的 `windeployqt` 去部署 Qt6 的 exe，报 `does not seem to be a Qt executable`；反过来会把 Qt5 DLL 拷到 Qt6 exe 旁边，启动即 `Cannot mix incompatible Qt libraries` | 构建与部署都指向同一个 kit；`package.py` 的 `DEF_QT` 与 `CMakeLists.txt` 的 `ICLOUDMON_QT_DIR` 必须一致 |
| 环境里存在大小写重复的变量 | 同时有 `HTTP_PROXY` 和 `http_proxy` 时，MSBuild 报 `MSB6001 … Key in dictionary`（VS 安装器报 `0x80070057`），报错栈全是 .NET 内部，看不出跟环境有关 | `package.py` 的 `run()` 统一用去重后的环境启动子进程；手工构建前 `set http_proxy=` 清一个 |
| `pragma Singleton` 必须配套 `qmldir` | QML 加载失败 | `resources/qmldir` 内容为 `singleton Theme 1.0 Theme.qml` |
| `--qmldir=ui` 必须给 | 目录选择对话框 `QtQuick/Dialogs` 不会被部署，运行时才报缺模块 | `build.bat` 已带该参数 |
| Qt6 的 `QtQuick.Dialogs` 与 Qt5 的 `1.3` 是两个实现 | `selectFolder` / `FileDialog.OpenDirectory` 在 Qt6 里都不存在；写错不报错，只打一条 `Unable to assign [undefined] to QQuickFileDialog::FileMode`，然后对话框按“选文件”打开 | 选目录用 **`FolderDialog`**（6.3+），取 `selectedFolder`；import 写无版本号的 `import QtQuick.Dialogs` |

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
| 保留 | 最近 30 天（换日时自动删除更早的） |
| 写入模式 | **追加**（重启后仍能看到上一次运行的日志） |
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
| 2 | 注册表 `HKCU\Software\iCloudMon\iCloudMon` 的 `driveRoot` | 用户在设置页选的路径，重启后仍有效 |
| 3 | **数据库自动推导** | 从 `iCloudDrive.db` 反推真实同步根（见下） |
| 4 | `%USERPROFILE%\iCloudDrive` | 标准安装位置，机器无关兜底 |

第 3 条是真正的权威来源：iCloud 把每个已知项的**完整本地绝对路径**写进
`iCloudDrive.db` 的 `local_items.item_absolutepath`（机器无关），从中向上找
「含 `.Trash` 子目录的最高祖先」即可推出同步根 —— 用户把 iCloud 目录挪到
别处也能自动找到，不需要手填。

设置页选目录时会做两项校验：目录必须存在、目录内应有 `.Trash` 回收站目录
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
C:\Users\<用户>\AppData\Roaming\iCloudMon\iCloudMon\icloud_state.jsonl
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
# 注: IcloudMonitor.cpp 已下线(直读 db 的职责移交 Python 旁路), 上面只是
# 历史排错记录。同样的坑现在在 sidecar/icloudmon/snapshot.py 里。
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

窗口最小化、或在别的虚拟桌面时，界面里的提示条根本看不到。
真正能提醒到人的只有系统通知（Windows 通知中心）。

**走过的弯路（都别再走一遍）**：

- `QSystemTrayIcon::showMessage()` 在本机**会死锁**：点「测试通知」精确卡在那一句
  （日志里它之前的行有、之后的行没有，进程只能强杀）。它最终落到系统的通知
  通道上，而调用线程的 COM 单元与通知通道要求的单元不一致时，跨单元 marshal
  会把调用方挂住。
- 退而求其次自绘过一个置顶浮层窗口（`ui/Toast.qml` + `NotificationHub`）。
  能用，但那**不是系统通知**：不进通知中心、错过就没了，观感也不像
  「系统在提醒你」。**已删除**。

**现在的做法**：`inc/SystemNotifier.h` / `src/SystemNotifier.cpp`

- 一个**专属 STA 线程**：线程内 `RoInitialize(RO_INIT_SINGLETHREADED)` +
  消息泵（`MsgWaitForMultipleObjectsEx` 同时等「新任务事件」和「窗口消息」）。
  UI 线程只做「入队 + `SetEvent`」，毫秒级返回，**绝不阻塞界面** ——
  通知发不出去也不能影响监控本身。
- 直接调 SDK 的 WinRT ABI 接口（`winrt/windows.ui.notifications.h` +
  `windows.data.xml.dom.h`），**不依赖 WRL / C++/WinRT** —— 本机 VS2019 没装
  `wrl/` 头，而 ABI 头再加上两个十几行的小工具（COM 指针 / HSTRING）就够。
  链接 `runtimeobject.lib`（Windows SDK 自带，非第三方）。
- 内容用系统模板 `ToastText02` + `CreateTextNode` 填，**不手拼 XML**
  （`& < >` 的转义交给系统）。
- 通知带**固定 tag**：同 tag 的新通知替换旧的。卡死是持续状态、每 5 秒上报一次，
  不去重就会把通知中心刷成一列。
- 用 `get_Setting()` 探测系统是否允许本应用弹通知；被关掉时置
  `Bridge.systemNotifyAvailable=false`，设置页显式提示，并让窗口内提醒
  **无条件补弹** —— 告警不能「看起来发了、实际没人看见」。
- 窗口**正好在前台**时，`Main.qml` 的 `onSystemNotify` 再同步弹一条窗口内提醒
  （人就在看窗口，这条才一定看得见）；窗口隐藏或不在前台时不弹 ——
  系统通知已经在屏幕右下角了，再弹一份就是同一件事说两遍。
- AUMID 用 `iCloudMon`：Windows 据此在
  `HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Notifications\Settings\iCloudMon`
  建一条用户可管理的通知设置项。**不需要注册开始菜单快捷方式** ——
  2026-10-13 实测：32 位 Release 与 64 位 Release（单文件版）两种构建下
  `Show()` 都返回 `S_OK`，通知确实出现在桌面右下角并进了通知中心。
- 托盘仍然要建（窗口关闭后它是唯一入口），且**必须有右键菜单**
  （显示主窗口 / 退出），否则关掉窗口后用户只能去任务管理器。

**编译坑（值得记一笔）**：SDK 里那个常量叫 `RO_INIT_SINGLETHREADED` ——
`SINGLE` 与 `THREADED` 之间**没有下划线**。写成 `RO_INIT_SINGLE_THREADED`
会报「未定义标识符」，很容易误判成 `WINAPI_FAMILY` 分区问题去翻头文件。

---

## 九、界面自测（离屏截图）

改 QML 后想肉验界面，但开发机常无交互桌面会话（无显示器 / 远程 / CI），
`sceneGraphInitialized` 不触发，常规 `grabToImage` 拿不到像素。程序内置一个
离屏截图钩子，用软件渲染后端把窗口渲成 PNG：

```bat
set QT_QUICK_BACKEND=software
set ICLOUDMON_SHOT=C:/tmp/shots
:: 可选：只截指定页（name 见 Main.qml 的 NavItem），逗号分隔；不设置则逐页全截
set ICLOUDMON_SHOT_PAGES=首页,设置
icloudmon.exe
```

- 钩子通过 `qInstallEventFilter` 监听窗口 `Expose`，在首个
  `sceneGraphInitialized` 之后按页切换并 `grabWindow()`；**不设
  `ICLOUDMON_SHOT` 时完全不生效**，零运行时开销。
- `QT_QUICK_BACKEND=software`（软件光栅化，不需要 GPU / 合成器）是无头环境
  拿到像素的前提；不设这个变量，场景图初始化事件不触发。
- 产物为 `C:/tmp/shots/<页名>.png`，逐页切、逐页截，可肉眼核对布局而不必真开窗口。

### 退出段错误（code 139）：已定位并修复

**症状**：退出时（`main` 返回后的析构阶段）进程以 `0xC0000005` 结束，退出码 139，
WER 在 `%LOCALAPPDATA%\CrashDumps\` 留下一个 5~8 MB 转储。用户无感 ——
`[RUN] ==== 退出 (code=0) ====` 已写出、窗口正常关闭、日志完整刷盘。

**根因**：`log::shutdown()` 调的是 `spdlog::shutdown()`，会把 default logger 一并销毁；
而 spdlog 的自由函数（`spdlog::info/warn/...`）内部是
`default_logger_raw()->log(...)`，**不做空指针检查**。`main()` 原来的收尾顺序恰好必然踩到：

```text
写 "[RUN] ==== 退出 (code=0) ===="  →  log::shutdown()  →  return rc
                                                          ↑ 栈对象从这里才开始析构
```

`Bridge` / `SidecarClient` / `IcloudMonitor` 等一大批对象的析构路径里都有日志
（例如 `SidecarClient::shutdown()` 要记 `[SIDECAR] 已退出`），于是每次正常退出都崩一次。

**证据**（`CrashDumps\iCloudMon.exe.7060.dmp`）：

| 项 | 值 |
|---|---|
| `ExceptionCode` | `0xC0000005`（访问违例） |
| `ExceptionInformation` | `{0, 0x40}` → 读地址 `0x40`（空 `this` + 成员偏移） |
| 出错地址 | `0x14000a178`，落在 `iCloudMon.exe` 自己的代码段 |
| 符号化结果 | `spdlog::logger::log<std::string>` |
| 旧 32 位转储对比 | 同 `0xC0000005`、同在 exe 内、读地址 `0x2c` → **同一个 bug，不是 Qt6/64 位迁移引入的** |

**修法**（两处配合，缺一不可）：

1. `main.cpp`：日志收尾交给 main 开头声明的 `LogShutdownGuard`。栈对象按声明顺序的
   倒序析构，"最先声明" = "最后析构"，所以关日志落在所有析构之后 —— 退出阶段的日志
   （如 `[SIDECAR] 已退出`）**照常写盘**。
2. `inc/Log.h`：`logAt()` 是**唯一**碰 spdlog 的地方，先问 `active()`；`shutdown()`
   先落闸再销毁 logger。闸门兜住静态析构、Qt 迟到消息这类 RAII 守卫管不到的情况 ——
   `main.cpp` 里 Qt 消息转发原先直接调 spdlog（窗口销毁阶段 Qt 仍会吐告警），一并改走门面。

**回归验证**：`ICLOUDMON_SHOT` 截图钩子跑完会自动 `quit()`，可重复触发同一条退出路径。

| | 退出码 | 新增 WER 转储 | 退出阶段日志 |
|---|---|---|---|
| 修复前 | **139**（SIGSEGV） | 有 | 停在 `退出 (code=0)` |
| 修复后（连跑 3 次） | **0** | 无 | 记到 `[SIDECAR] 已退出 (exit=0)` |

---

### 日志每次启动被清空 + 30 天保留失效：已定位并修复

**症状**（v1.0.6 之前）：同一天内重启程序，`log\icloudmon_<日期>` 被清空重写，
**上一次运行的日志直接消失**；同时文档里写的「保留最近 30 天」从未生效，
旧的日切文件会一直堆在 `log\` 下无人清理。

**根因**：`spdlog::sinks::daily_file_sink_mt` 的真实签名是

```text
(文件名, 小时, 分钟, truncate, max_files, file_event_handlers)
```

第 4 个参数是 **`bool truncate`**，不是 `max_files`。原代码按
`(base, 23, 59, kMaxFiles, kMaxSizePerDay)` 的顺序传，参数整体错位一格：

| 传入 | 落到的形参 | 实际效果 |
|---|---|---|
| `kMaxFiles` = 30 | `bool truncate` | `30 != 0` → **true，每次启动截断** |
| `kMaxSizePerDay` = 20971520 | `uint16_t max_files` | 20971520 % 65536 = **0 → 从不清理** |

「追加写」与「保留 30 天」两个目的**一个都没达成**；而这两个参数传错不会报错
（`bool` 能吃任何整数、`uint16_t` 只是静默窄化），所以长期没被发现。

**修法**：按真实签名传 `(base, 23, 59, /*truncate=*/false, kMaxFiles)`。
另外 `daily_file_sink` **只能按时间切**，没有按大小切的能力，原注释里
「单日超 20 MB 再切」本就无法实现，已连同 `kMaxSizePerDay` 一起去掉，文档同步更正。

**验证**：连跑 3 次，日志文件逐次**变长**（追加生效），启动记录条数递增，不再被清空。

| | 文件大小 | 启动记录条数 |
|---|---|---|
| 修复前（连跑 3 次） | 每次都是 2473 B（被清空重写） | 恒为 1 |
| 修复后（连跑 3 次） | 逐次递增 | 1 → 2 → 3 |

---

## 十、第三方与许可

| 组件 | 版本 | 许可 | 位置 |
|---|---|---|---|
| spdlog | v1.14.1 | MIT | `third_party/spdlog/`（含 LICENSE） |
| Qt | 6.8.3（msvc2022_64） | LGPL v3 / 商业 | 外部安装，不入库 |
| Python | 3.13（embeddable） | PSF | 打包时下载到 `build-tools/`，不入库 |
| iCloud 图标 | selfhst/icloud（Iconify），官方彩色渐变 SVG | 商标归 Apple Inc. | `resources/icloud.svg`；本项目仅作只读展示用途，不修改、不重新分发变形版本 |

iCloud Drive 的状态库由 Apple 创建，本项目**只读**，不修改、不上传。