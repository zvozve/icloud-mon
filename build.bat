@echo off
REM =====================================================================
REM  build.bat  -  iCloudMon one-click build script (ASCII only!)
REM
REM  NOTE: This file MUST stay pure ASCII.
REM        cmd.exe on zh-CN reads .bat as GBK; any UTF-8 Chinese comment
REM        will be chopped into garbage tokens and break the build.
REM
REM  Steps: vcvars32 -> cmake configure -> build -> windeployqt
REM =====================================================================

setlocal

set VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars32.bat
set QTDIR=C:/Container/System/Library/QT/5.15.2/msvc2019
set BUILDDIR=build
set TARGET=iCloudMon.exe

echo ============================================================
echo  iCloudMon build  (Qt 5.15.2 msvc2019 32bit)
echo ============================================================
echo.

if not exist "%VCVARS%" (
    echo [ERROR] vcvars32.bat not found:
    echo         %VCVARS%
    exit /b 1
)

REM --- 1. MSVC 32bit environment -----------------------------------
echo [1/4] Loading MSVC x86 environment ...
call "%VCVARS%" >nul
if errorlevel 1 (
    echo [ERROR] vcvars32 failed.
    exit /b 1
)
echocl.exe 2>nul
echo       cl: & cl 2>&1 | findstr /C:"Version"
echo.

REM --- 2. cmake configure ------------------------------------------
echo [2/4] Configuring with CMake (Ninja) ...
cmake -S . -B %BUILDDIR% -G Ninja ^
      -DCMAKE_PREFIX_PATH=%QTDIR% ^
      -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    echo [ERROR] cmake configure failed.
    exit /b 1
)
echo.

REM --- 3. build -----------------------------------------------------
echo [3/4] Building ...
cmake --build %BUILDDIR%
if errorlevel 1 (
    echo [ERROR] build failed.
    exit /b 1
)
echo.

REM --- 4. windeployqt ------------------------------------------------
echo [4/4] Deploying Qt runtime ...
"%QTDIR%\bin\windeployqt.exe" --release --32bit --no-translations ^
        --qmldir=ui %BUILDDIR%\%TARGET%
if errorlevel 1 (
    echo [WARN] windeployqt returned %errorlevel%, exe may miss some DLLs.
)
echo.

echo ============================================================
echo  BUILD OK  ->  %BUILDDIR%\%TARGET%
echo  Run it with:  %BUILDDIR%\%TARGET%
echo ============================================================

endlocal