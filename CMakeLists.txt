cmake_minimum_required(VERSION 3.16)
project(iCloudMon VERSION 1.0.0 LANGUAGES CXX)

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
# SPDLOG_HEADER_ONLY 必须 PRIVATE: 这宏会污染编译期命名空间, 不能外泄
target_compile_definitions(spdlog_header_only INTERFACE SPDLOG_HEADER_ONLY)

# ======================================================================
#  可执行目标
# ======================================================================
add_executable(iCloudMon WIN32
    src/main.cpp
    src/Bridge.cpp
    src/IcloudMonitor.cpp
    src/IcloudCategories.cpp
    src/IcloudService.cpp
    src/AutoStart.cpp
    src/Log.cpp
    # 头文件必须显式列出: AUTOMOC 只扫描 target sources 里的文件,
    # 放在 inc/ 下不列出来的话 moc 不会被触发, 表现为
    # "无法解析的外部符号 staticMetaObject / qt_metacall ..."。
    inc/Bridge.h
    inc/IcloudMonitor.h
    inc/IcloudCategories.h
    inc/IcloudService.h
    inc/IcloudSnapshot.h
    inc/AutoStart.h
    inc/IcloudPaths.h
    inc/Log.h
    # 资源: 由 AUTORCC 编译, 并按 qrc 里引用的文件建立依赖
    resources/icloudmon.qrc
)

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
