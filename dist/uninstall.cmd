@echo off
setlocal
title OBS Auto Capture - uninstall

set "DST=%APPDATA%\obs-studio\plugins\obs-auto-capture"

echo.
echo   OBS Auto Capture - uninstaller
echo   Removing: %DST%
echo.

if not exist "%DST%" (
  echo   Nothing to remove.
  echo.
  pause
  exit /b 0
)

tasklist /fi "imagename eq obs64.exe" 2>nul | find /i "obs64.exe" >nul
if not errorlevel 1 (
  echo   WARNING: OBS Studio is running. Close it first, then press any key.
  echo.
  pause
)

rmdir /s /q "%DST%"
echo   Done.
echo.
pause
