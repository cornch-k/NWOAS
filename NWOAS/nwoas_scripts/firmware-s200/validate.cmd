@echo off
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; Write-Output ('UTC='+[DateTime]::UtcNow.ToString('o')); Get-CimInstance Win32_ComputerSystem | Select TotalPhysicalMemory,NumberOfLogicalProcessors | Format-List"
D:\CPUSTRES.EXE
if errorlevel 1 exit /b %errorlevel%
D:\DISKREAD.EXE
if errorlevel 1 exit /b %errorlevel%
C:\NWOAS-S195\IOTEST.EXE
exit /b %errorlevel%
