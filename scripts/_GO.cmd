@echo off
python "%~dp0verify.py" full --push %*
exit /b %errorlevel%
