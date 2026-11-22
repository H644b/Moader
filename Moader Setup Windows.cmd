@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup\windows-launch.ps1" "%~dp0"
if errorlevel 1 pause
