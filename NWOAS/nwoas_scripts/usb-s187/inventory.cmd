@echo off
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; Write-Output ('UTC='+[DateTime]::UtcNow.ToString('o')); Get-CimInstance Win32_PnPEntity | Where-Object {$_.PNPDeviceID -match '^(USB|HID)|XHC|FL1100'} | Select Name,PNPDeviceID,ConfigManagerErrorCode,Status | Format-List; Get-CimInstance Win32_ComputerSystem | Select TotalPhysicalMemory,NumberOfLogicalProcessors | Format-List"
D:\CPUSTRES.EXE
if errorlevel 1 exit /b %errorlevel%
D:\DISKREAD.EXE
exit /b %errorlevel%
