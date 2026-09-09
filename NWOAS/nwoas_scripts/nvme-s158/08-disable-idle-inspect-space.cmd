@echo off
setlocal EnableExtensions
chcp 65001 >nul
if /i not "%SystemDrive%"=="C:" exit /b 20
reg query HKLM\SYSTEM\CurrentControlSet\Control\MiniNT >nul 2>&1
if not errorlevel 1 exit /b 21
if not exist C:\Windows\Temp\NWOAS-S158-POWER-BEFORE.pow (
  powercfg /export C:\Windows\Temp\NWOAS-S158-POWER-BEFORE.pow 381b4222-f694-41f0-9685-ff5bb260df2e
  if errorlevel 1 exit /b 22
)
powercfg /change standby-timeout-ac 0
if errorlevel 1 exit /b 23
powercfg /change standby-timeout-dc 0
if errorlevel 1 exit /b 24
powercfg /change monitor-timeout-ac 0
if errorlevel 1 exit /b 25
powercfg /change monitor-timeout-dc 0
if errorlevel 1 exit /b 26
echo === S158 idle disabled; root files and space ===
powershell -NoProfile -Command "Get-ChildItem C:\ -Force | Select-Object Name,Length,Attributes | Format-Table -AutoSize; Get-CimInstance Win32_LogicalDisk -Filter 'DeviceID=''C:''' | Select-Object DeviceID,Size,FreeSpace | Format-List; Get-ChildItem C:\Windows\Temp -Force -File -ErrorAction SilentlyContinue | Sort-Object Length -Descending | Select-Object -First 12 Name,Length | Format-Table -AutoSize; Get-Item C:\Windows\MEMORY.DMP -Force -ErrorAction SilentlyContinue | Select-Object FullName,Length | Format-List"
exit /b 0
