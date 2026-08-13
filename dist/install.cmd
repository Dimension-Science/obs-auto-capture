@echo off
setlocal
title OBS Auto Capture - install

set "SRC=%~dp0obs-auto-capture"
set "DST=%APPDATA%\obs-studio\plugins\obs-auto-capture"

echo.
echo   OBS Auto Capture - installer
echo   ---------------------------------------------
echo   From : %SRC%
echo   To   : %DST%
echo.

if not exist "%SRC%\bin\64bit\obs-auto-capture.dll" (
  echo   ERROR: obs-auto-capture.dll not found next to this script.
  echo   Unpack the whole archive and run install.cmd from the unpacked folder.
  echo.
  pause
  exit /b 1
)

tasklist /fi "imagename eq obs64.exe" 2>nul | find /i "obs64.exe" >nul
if not errorlevel 1 (
  echo   WARNING: OBS Studio is running. Close it first, then press any key.
  echo.
  pause
)

if not exist "%APPDATA%\obs-studio" (
  echo   ERROR: OBS Studio settings folder was not found.
  echo   Start OBS Studio at least once, then run this installer again.
  echo.
  pause
  exit /b 1
)

xcopy "%SRC%" "%DST%\" /E /I /Y >nul
if errorlevel 1 (
  echo   ERROR: copying failed.
  echo.
  pause
  exit /b 1
)

echo   Done. Start OBS Studio and add the source:
echo   "+" in Sources  ^>  Auto App Capture
echo.
pause
