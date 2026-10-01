@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0software\launch_codex_bridge.ps1"
set "BRIDGE_EXIT_CODE=%ERRORLEVEL%"
if not "%BRIDGE_EXIT_CODE%"=="0" pause
exit /b %BRIDGE_EXIT_CODE%
