@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\mirror_creator\Start-MirrorCreator.ps1"
if errorlevel 1 pause
