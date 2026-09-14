@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0queue-status.ps1" %*
endlocal
