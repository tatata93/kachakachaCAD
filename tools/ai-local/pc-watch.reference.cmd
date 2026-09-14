@echo off
setlocal enabledelayedexpansion
REM Watch for to-pc.bundle from the cloud; build, test and push when it arrives.
REM Start this once; after that the cloud side needs no screen interaction.
REM To stop: press Ctrl+C twice in this window.
REM
REM 2026-09-14: this window stopped once and nothing said so. From the cloud it
REM looked exactly like a PC that had gone quiet. So it now writes a heartbeat:
REM if _claudeout\watch.log is not moving, this window is not running.
set "ROOT=%~dp0"
set "BUNDLE=%ROOT%to-pc.bundle"
set "SEEN=%ROOT%_claudeout\seen.txt"
set "BEAT=%ROOT%_claudeout\watch.log"
if not exist "%ROOT%_claudeout" mkdir "%ROOT%_claudeout"
title kachakachaCAD claude watcher
echo === claude watcher started %DATE% %TIME% ===
echo === claude watcher started %DATE% %TIME% === > "%BEAT%"
set /a BEATCOUNT=0
:loop
set /a BEATCOUNT+=1
REM One line per poll, kept short. Overwrite rather than grow without end.
> "%BEAT%" echo alive %DATE% %TIME% polls=!BEATCOUNT!
if exist "%BUNDLE%" (
  for %%F in ("%BUNDLE%") do set "STAMP=%%~tF %%~zF"
  set "LAST="
  if exist "%SEEN%" set /p LAST=<"%SEEN%"
  if not "!STAMP!"=="!LAST!" (
    echo [watch] new bundle: !STAMP!
    >> "%BEAT%" echo picked up !STAMP!
    REM Kill leftovers first, so an aborted previous run does not carry over.
    taskkill /F /IM kachakacha_cad_next.exe > nul 2>&1
    REM Watchdog the whole build too, so control always returns here.
    powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%_RUN_APP.ps1" 3600 "%ComSpec%" /c "%ROOT%_FIX_AND_BUILD.cmd"
    REM Do NOT kill the app here: step [7] leaves it running for the owner on purpose.
    >"%SEEN%" echo !STAMP!
    echo [watch] done. waiting for the next bundle...
  )
)
timeout /t 15 /nobreak > nul
goto loop
