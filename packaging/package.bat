@echo off
REM =====================================================================
REM  package.bat  -  one-click single-file packaging (ASCII only!)
REM
REM  Wrapper around packaging/package.py:
REM    build (MSVC) -> windeployqt -> Enigma Virtual Box -> dist\iCloudMon.exe
REM
REM  Usage:
REM    package.bat                     use version from CMakeLists.txt
REM    package.bat --version 1.0.4     set version then package
REM    package.bat --skip-build        repack only (no rebuild)
REM
REM  NOTE: keep this file pure ASCII (cmd.exe reads .bat as GBK on zh-CN).
REM =====================================================================
setlocal
set "PYEXE=python"
where py >nul 2>nul && set "PYEXE=py"
%PYEXE% "%~dp0package.py" %*
endlocal
