@echo off
setlocal
pwsh -NoProfile -File "%~dp0scripts\run_validation.ps1" %*
set "result=%ERRORLEVEL%"
if /I not "%~1"=="-NoPause" pause
exit /b %result%
