@echo off
setlocal
fltmc >nul 2>&1
if errorlevel 1 (
  powershell.exe -NoProfile -Command "Start-Process -Verb RunAs -FilePath '%~f0'"
  exit /b
)
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0collect.ps1"
echo.
echo Copy C:\NWOAS\S134-Windows-inventory.zip back to the Mac for analysis.
pause
