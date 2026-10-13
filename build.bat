@echo off
REM =====================================================================
REM  build.bat  -  iCloudMon one-click build script (ASCII only!)
REM
REM  NOTE: This file MUST stay pure ASCII.
REM        cmd.exe on zh-CN reads .bat as GBK; any UTF-8 Chinese comment
REM        will be chopped into garbage tokens and break the build.
REM
REM  Toolchain since 1.0.5: MSVC 2022 64bit + Qt 6.8.3 msvc2022_64
REM  (1.0.4 and older: MSVC 2019 32bit + Qt 5.15.2 msvc2019.
REM   The old 32-bit build tree stays in build32\ for reference, and the
REM   Qt5 32-bit kit stays installed -- the company projects still use it,
REM   the two sets coexist and do not interfere.)
REM
REM  Steps: vcvars64 -> cmake configure -> build -> windeployqt
REM
REM  For a full single-file release use packaging\package.py instead:
REM  it does the same plus embedding the Python sidecar runtime.
REM =====================================================================

setlocal

set VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat
set QTDIR=C:/Container/System/Library/QT/6.8.3/msvc2022_64
set BUILDDIR=build
set TARGET=iCloudMon.exe

echo ============================================================
echo  iCloudMon build  (Qt 6.8.3 msvc2022 64bit)
echo ============================================================
echo.

REM NOTE: the vcvars path contains "(x86)". Inside a parenthesized if-block
REM cmd miscounts the parentheses of the expanded path and tries to execute
REM "\Microsoft ...", aborting with "\Microsoft was unexpected at this time".
REM So the existence check must NOT use a (...) block -- use goto instead.
if exist "%VCVARS%" goto :vc_ok
echo [ERROR] vcvars64.bat not found:
echo         %VCVARS%
echo         Install "Visual Studio 2022 Build Tools" with the C++ workload.
exit /b 1
:vc_ok

REM --- 1. MSVC 64bit environment -----------------------------------
echo [1/4] Loading MSVC x64 environment ...
call "%VCVARS%" >nul
if errorlevel 1 (
    echo [ERROR] vcvars64 failed.
    exit /b 1
)
echo       cl: & cl 2>&1 | findstr /C:"Version"
echo.

REM --- 2. cmake configure ------------------------------------------
REM The generator MUST match the cache already in build\ (Visual Studio
REM 17 2022 / x64). Changing generator mid-tree makes CMake refuse to
REM reconfigure -- delete build\ if you really want to switch it.
echo [2/4] Configuring with CMake (Visual Studio 17 2022 / x64) ...
cmake -S . -B %BUILDDIR% -G "Visual Studio 17 2022" -A x64 ^
      -DCMAKE_PREFIX_PATH=%QTDIR%
if errorlevel 1 (
    echo [ERROR] cmake configure failed.
    exit /b 1
)
echo.

REM --- 3. build -----------------------------------------------------
echo [3/4] Building ...
cmake --build %BUILDDIR% --config Release
if errorlevel 1 (
    echo [ERROR] build failed.
    exit /b 1
)
echo.

REM --- 4. windeployqt ------------------------------------------------
echo [4/4] Deploying Qt runtime ...
REM --qmldir=ui is required so windeployqt scans the QML imports and
REM pulls in QtQuick/Dialogs (used by the folder picker) as well as
REM QtQuick/Controls. Omitting it yields a silent QML load failure.
REM NOTE: use the windeployqt from the SAME kit as the build (Qt6 here).
REM       Mixing kits drops Qt5 DLLs next to a Qt6 exe and the app dies
REM       at startup with "Cannot mix incompatible Qt libraries".
"%QTDIR%\bin\windeployqt.exe" --release --no-translations ^
        --qmldir=ui %BUILDDIR%\Release\%TARGET%
if errorlevel 1 (
    echo [WARN] windeployqt returned %errorlevel%, exe may miss some DLLs.
)
echo.

echo ============================================================
echo  BUILD OK  ->  %BUILDDIR%\Release\%TARGET%
echo  Run it with:  %BUILDDIR%\Release\%TARGET%
echo ============================================================

endlocal
