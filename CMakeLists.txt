cmake_minimum_required(VERSION 3.16)
project(iCloudMon VERSION 1.0.4 LANGUAGES CXX)

# ======================================================================
#  iCloudMon - iCloud Drive 同步状态监控器
#
#  构建 (MSVC 2019 32bit + Qt 5.15.2):
#    见同目录 build.bat  (已封装 vcvars32 + cmake + ninja)
#
#  手动构建示例:
#    call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\
#         VC\Auxiliary\Build\vcvars32.bat"
#    cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=<Qt路径> -DCMAKE_BUILD_TYPE=Release
#    cmake --build build
# ======================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
# ★ AUTORCC 必须开。
#
# 曾经的写法是关掉 AUTORCC + 用 qt5_add_resources。问题: qt5_add_resources
# 生成的 rcc 产物**只把 .qrc 自身当作依赖**, .qrc 里引用的 ui/*.qml 不在依赖里。
# 后果是只改 QML(界面) 不触发资源重新打包 —— F7 编译"成功", 跑出来的却还是
# 上一次的界面, 白白怀疑代码写错了。这个坑非常隐蔽。
#
# AUTORCC 会解析 .qrc, 把里面 <file> 引用的文件一并列为 rcc 的依赖,
# 改 qml / svg 就会重新打包。详见下方 add_executable 里的 icloudmon.qrc。
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Qt 5.15 需要, 否则 QML 报 Cannot mix incompatible Qt libraries
if(NOT DEFINED QT_HOST_PATH)
    # 构建机路径; 同时允许外部覆盖
    set(QT_HOST_PATH "C:/Container/System/Library/QT/5.15.2/msvc2019")
endif()

find_package(Qt5 5.15 REQUIRED COMPONENTS
    Core Gui Qml Quick QuickControls2 Sql
    # Widgets 仅用于 QSystemTrayIcon(卡死时的 Windows 系统通知)。
    # 注意: QSystemTrayIcon 属于 QtWidgets, 不是 QtGui。
    # 部署代价为零 —— windeployqt 本来就已带上 Qt5Widgets.dll
    # (QtQuick.Dialogs 依赖 QtQuick.PrivateWidgets)。
    Widgets
)

# Qt 资源文件 (QML 打包进 exe, 免外部文件依赖)
# 直接把 .qrc 交给 target, 由 AUTORCC 处理并跟踪它引用的文件。
# 不要再改成 qt5_add_resources —— 它不会跟踪 qrc 引用的文件(见文件开头说明)。

# ======================================================================
#  spdlog (vendored)
#
#  第三方日志库, 源码直接放仓库里, 不依赖 vcpkg 等包管理器
#  —— 与本项目"第三方库随仓库走"的约定一致(同 STM32 SDK 的 MiddleWare/)。
#  用 header-only 模式, 只引入 include/, 编译进 exe, 不产生额外 DLL。
# ======================================================================
add_library(spdlog_header_only INTERFACE)
target_include_directories(spdlog_header_only INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/spdlog/include)
# ★ 刻意**不**在这里定义 SPDLOG_HEADER_ONLY。
#   spdlog 自己的 spdlog/common.h 里已经有一句无条件的
#     #if !defined(SPDLOG_COMPILED_LIB)
#     #  define SPDLOG_HEADER_ONLY
#     #endif
#   也就是说"没定义 SPDLOG_COMPILED_LIB 就等于 header-only 模式",
#   是 spdlog 的默认行为。再用 -D 从命令行补一遍就是**重复定义**,
#   MSVC 每编一个 TU 都报一次
#     warning C4005: 'SPDLOG_HEADER_ONLY': 宏重定义
#   (之前一次改 5 个文件, 刷出十几条一模一样的警告, 噪声极大)。
#   要切回 compiled 模式时, 定义 SPDLOG_COMPILED_LIB 才是对的开关。

# ======================================================================
#  exe 的 PE 资源(图标 + 版本信息)
#
#  Explorer/任务栏/Alt-Tab 的文件图标来自 PE 资源里的 .ico, 与运行时的
#  setWindowIcon 是两套机制。resources/icloud.ico 由 packaging/make_ico
#  工具从 icloud.svg 生成(多尺寸 PNG-in-ICO); 若不存在则只给 warning,
#  不阻断构建(没有图标 exe 仍能运行, 只是文件图标是默认的)。
#
#  用模板 + configure_file 注入: rc.exe 不按 .rc 所在目录解析 ICON 相对
#  路径, 必须给绝对路径才稳, 而绝对路径不该写死在版本库里。
# ======================================================================
set(ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/icloud.ico")
if(NOT EXISTS "${ICON_PATH}")
    message(WARNING "resources/icloud.ico 不存在, exe 将没有文件图标。"
                    " 可运行 packaging/make_ico 从 icloud.svg 生成。")
    set(RC_GEN "")
else()
    set(RC_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.rc.in")
    set(RC_GEN "${CMAKE_CURRENT_BINARY_DIR}/iCloudMon_resources.rc")
    # VERSIONINFO 要逐段数字, 从 project(VERSION) 拆出来
    set(VER_MAJOR ${PROJECT_VERSION_MAJOR})
    set(VER_MINOR ${PROJECT_VERSION_MINOR})
    set(VER_PATCH ${PROJECT_VERSION_PATCH})
    configure_file("${RC_TEMPLATE}" "${RC_GEN}" @ONLY)
endif()

# ======================================================================
#  可执行目标
# ======================================================================
add_executable(iCloudMon WIN32
    src/main.cpp
    src/Bridge.cpp
    # 数据源已切到 Python 旁路(dev 分支): SidecarClient 拉起 sidecar/serve.py,
    # 经 NDJSON 回传, Bridge 只读 payload。见 doc/架构边界与风险控制.md
    src/SidecarClient.cpp
    # Snapshot 的纯数据派生方法(stateName/stateDetail/isBusy)。本来住在
    # IcloudMonitor.cpp 里, 但与 monitor 无关; IcloudMonitor 下线后
    # IcloudCategories 仍要用, 因此独立成编译单元。
    src/IcloudSnapshot.cpp
    # ★ IcloudMonitor(C++ 直读 iCloudDrive.db)已下线 —— 它的职责整体交给
    #   Python 旁路(谁做得更好用谁: 读 db 这件事 Python 更灵活, 且已实测 45~96ms)。
    #   文件保留在仓库里作参考, 不再参与构建; 确认旁路稳定后即可删除。
    # src/IcloudMonitor.cpp
    src/IcloudCategories.cpp
    # iCloud 存储配额(首页存储卡片): 解析器未调通, 暂不参与构建。
    # 代码保留, 等 BPlist 解析器验证通过后取消注释即可。
    # src/IcloudQuota.cpp
    # src/BPlist.cpp
    src/IcloudService.cpp
    src/AutoStart.cpp
    src/Log.cpp
    src/NotificationHub.cpp
    # 头文件必须显式列出: AUTOMOC 只扫描 target sources 里的文件,
    # 放在 inc/ 下不列出来的话 moc 不会被触发, 表现为
    # "无法解析的外部符号 staticMetaObject / qt_metacall ..."。
    inc/Bridge.h
    inc/SidecarClient.h
    # 见上: IcloudMonitor 已下线, 头文件不再需要 moc
    # inc/IcloudMonitor.h
    inc/IcloudCategories.h
    inc/NotificationHub.h
    inc/IcloudService.h
    inc/IcloudSnapshot.h
    inc/AutoStart.h
    inc/IcloudPaths.h
    inc/Log.h
    # 资源: 由 AUTORCC 编译, 并按 qrc 里引用的文件建立依赖
    resources/icloudmon.qrc
)

# 把生成的 .rc(图标+版本)加入 target, 嵌进 exe 的 PE 资源
if(RC_GEN)
    target_sources(iCloudMon PRIVATE "${RC_GEN}")
endif()

target_include_directories(iCloudMon PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

target_link_libraries(iCloudMon PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Qml
    Qt5::Quick
    Qt5::QuickControls2
    Qt5::Sql
    Qt5::Widgets
    spdlog_header_only
)

# 32 位校验: 与 msvc2019(32bit) 套件对齐
target_compile_definitions(iCloudMon PRIVATE
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    # 版本号单一真相源: 只在文件首行 project(VERSION) 维护一处,
    # 编译期注入到代码。main.cpp 用 QStringLiteral(APP_VERSION) 引用,
    # 不再手写版本字符串 —— 避免"改了 CMake 忘了改代码"的漂移。
    APP_VERSION="${PROJECT_VERSION}"
)

if(MSVC)
    target_compile_options(iCloudMon PRIVATE
        /W3                       # 警告等级
        /utf-8                    # 源码按 UTF-8 处理, 否则中文乱码
        /wd4996                   # 屏蔽 std::iterator 的 deprecate 警告
        /bigobj                   # spdlog 头文件极多, 否则 C1128
    )
    # 调试版生成 PDB
    target_link_options(iCloudMon PRIVATE /DEBUG)
endif()
