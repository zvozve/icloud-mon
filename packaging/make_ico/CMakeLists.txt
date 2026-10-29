cmake_minimum_required(VERSION 3.16)
project(make_ico LANGUAGES CXX)

# ======================================================================
#  make_ico - 构建期小工具: 把 resources/icloud.svg 转成多尺寸 icloud.ico
#  用来把图标嵌进 exe 的 PE 资源(Explorer/任务栏/Alt-Tab 显示)。
#  不是主程序的一部分, 单独构建。
#
#  构建:
#    cmake -S packaging/make_ico -B build/make_ico -G "Visual Studio 16 2019" -A Win32 ^
#          -DCMAKE_PREFIX_PATH=<Qt>
#    cmake --build build/make_ico --config Release
# ======================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

if(NOT DEFINED QT_HOST_PATH)
    set(QT_HOST_PATH "C:/Container/Applications/QT/5.15.2/msvc2019")
endif()

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Svg)

add_executable(make_ico WIN32 main.cpp)

target_link_libraries(make_ico PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Svg
)

if(MSVC)
    target_compile_options(make_ico PRIVATE /utf-8)
endif()