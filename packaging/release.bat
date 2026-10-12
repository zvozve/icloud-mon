@echo off
REM =====================================================================
REM  release.bat  -  publish the packed exe to GitHub Releases (ASCII only!)
REM
REM  Wrapper around packaging/release.py (uses GitHub REST API, no gh needed).
REM  Token: set GITHUB_TOKEN in the environment first.
REM
REM  Usage:
REM    set GITHUB_TOKEN=ghp_xxx
REM    release.bat                        publish current version
REM    release.bat --version 1.0.3
REM    release.bat --draft                publish as draft first
REM
REM  NOTE: keep this file pure ASCII (cmd.exe reads .bat as GBK on zh-CN).
REM =====================================================================
setlocal
set "PYEXE=python"
where py >nul 2>nul && set "PYEXE=py"
%PYEXE% "%~dp0release.py" %*
endlocal
