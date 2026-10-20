@echo off
setlocal DisableDelayedExpansion
pwsh.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0clean_test_outputs.ps1" %*
exit /b %errorlevel%
