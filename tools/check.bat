rem SPDX-License-Identifier: GPL-3.0-or-later
@echo off
setlocal

rem Windows/MSVC 唯一推荐的提交前自检入口。
rem 这里不调用 bash，也不需要 Linux / WSL / Git Bash。

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0check.ps1" %*
exit /b %ERRORLEVEL%
