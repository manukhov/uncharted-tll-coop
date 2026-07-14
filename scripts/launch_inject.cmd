@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0launch_inject.ps1" %*
