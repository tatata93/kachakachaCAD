@echo off
python "%~dp0verify.py" focused %*
exit /b %errorlevel%
