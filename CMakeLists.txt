cmake_minimum_required(VERSION 3.16)
project(iCloudMon VERSION 1.0.5 LANGUAGES CXX)

# ======================================================================
#  iCloudMon - iCloud Drive 同步状态监控器
#
#  构建 (MSVC 2022 64bit + Qt 6.8.3):
#    见 packaging/package.py (编译 -> windeployqt -> Enigma 单文件)
#    或同目录 build.bat (已封装 vcvars64 + cmake)
#
#  手动构建示例:
#    call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\
#         VC\Auxiliary\Build\vcvars64.bat"
#    cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
#          -DCMAKE_PREFIX_PATH=C:/Container/System/Library/QT/6.8.3/msvc2022_64
#    cmake --build build --config Release
#
#  ★ 工具链变更史(别被旧注释误导):
#    1.0.4 及以前 = MSVC 2019 32bit + Qt 5.15.2 msvc2019。
#    1.0.5 起    = MSVC 2022 64bit + Qt 6.8.3 msvc2022_64。
#    32 位的旧构建目录保留在 build32/(不进版本库), 用于随时对照。
#    Qt5 32 位套件仍留在 C:/Container/System/Library/QT/5.15.2/msvc2019,
#    公司项目继续用它, 与本工程互不影响。
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

# Qt 安装目录(默认构建机路径; 可用 -DICLOUDMON_QT_DIR=... 覆盖)
if(NOT DEFINED ICLOUDMON_QT_DIR)
    set(ICLOUDMON_QT_DIR "C:/Container/System/Library/QT/6.8.3/msvc2022_64")
endif()
list(APPEND CMAKE_PREFIX_PATH "${ICLOUDMON_QT_DIR}")

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core Gui Qml Quick QuickControls2 Sql
    # Widgets 用于 QSystemTrayIcon + QMenu。
    # 注意: 别信"Qt6 把 QSystemTrayIcon 挪到 QtGui 了"的说法 —— 在 6.8 里
    # 它仍然在 QtWidgets(include/QtWidgets/QSystemTrayIcon 实测存在),
    # 所以 Widgets 不能去掉, 否则链接期 undefined symbol。
    # 顺带一提 QOpenGLContext / QOpenGLFunctions 在 6.8 也仍在 QtGui,
    # 只有 QOpenGLShaderProgram 那一批挪去了 QtOpenGL 模块。
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
    # Windows 系统通知(通知中心)。内部是一个专属 STA 线程 + 消息泵:
    # Qt 的 QSystemTrayIcon::showMessage 在本机会死锁(单元不匹配), 详见 .cpp 头注释。
    src/SystemNotifier.cpp
    # 头文件必须显式列出: AUTOMOC 只扫描 target sources 里的文件,
    # 放在 inc/ 下不列出来的话 moc 不会被触发, 表现为
    # "无法解析的外部符号 staticMetaObject / qt_metacall ..."。
    inc/Bridge.h
    inc/SidecarClient.h
    # 见上: IcloudMonitor 已下线, 头文件不再需要 moc
    # inc/IcloudMonitor.h
    inc/IcloudCategories.h
    inc/SystemNotifier.h
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
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Sql
    Qt6::Widgets
    spdlog_header_only
)

if(WIN32)
    # main.cpp 用 RegDeleteTree/RegDeleteKey 清理旧版留下的空注册表键
    # (QSettings::clear 删不掉 org 父键), 需要 Advapi32。
    target_link_libraries(iCloudMon PRIVATE Advapi32)
    # SystemNotifier 走 WinRT 发系统通知: RoInitialize / RoGetActivationFactory
    # / RoActivateInstance 都在 runtimeobject.lib(Windows SDK, 不是第三方库)。
    target_link_libraries(iCloudMon PRIVATE runtimeobject.lib)
endif()

# 编译选项: 与 msvc2022_64(64 位) 套件对齐
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
