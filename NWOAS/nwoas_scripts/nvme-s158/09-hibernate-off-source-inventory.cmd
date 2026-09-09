@echo off
setlocal EnableExtensions
chcp 65001 >nul
if /i not "%SystemDrive%"=="C:" exit /b 20
reg query HKLM\SYSTEM\CurrentControlSet\Control\MiniNT >nul 2>&1
if not errorlevel 1 exit /b 21
powercfg /hibernate off
if errorlevel 1 exit /b 22
echo === S158 hibernation disabled; source inventory ===
powershell -NoProfile -Command "Get-CimInstance Win32_LogicalDisk -Filter 'DeviceID=''C:''' | Select-Object DeviceID,Size,FreeSpace | Format-List; Get-ChildItem C:\S117SRC,C:\S124SCRATCH,C:\NWOAS124 -Force -Recurse -File -ErrorAction SilentlyContinue | Sort-Object Length -Descending | Select-Object -First 20 FullName,Length | Format-Table -AutoSize"
exit /b 0
