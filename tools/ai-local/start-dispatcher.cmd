@echo off
REM Start the local review dispatcher once. Leave this window open.
REM It watches .ai-runtime\incoming and starts a reviewer only for requests that
REM the machine has already built and tested. Stop it with Ctrl+C.
setlocal
set "HERE=%~dp0"
title kachakachaCAD review dispatcher
powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%review-dispatcher.ps1" %*
endlocal
