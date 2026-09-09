@echo off
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; Write-Output ('S222 UTC='+[DateTime]::UtcNow.ToString('o')); Write-Output 'S222 CPU metadata (not measured clock)'; Get-CimInstance Win32_Processor | Select Name,NumberOfCores,NumberOfLogicalProcessors,MaxClockSpeed,CurrentClockSpeed | Format-List; Write-Output 'S222 MEMORY'; Get-CimInstance Win32_ComputerSystem | Select TotalPhysicalMemory,NumberOfLogicalProcessors | Format-List; Write-Output 'S222 DEVICE PROBLEMS (no instance IDs)'; Get-CimInstance Win32_PnPEntity | Where-Object {$_.ConfigManagerErrorCode -ne 0} | Select Name,PNPClass,ConfigManagerErrorCode | Format-Table -AutoSize; Write-Output 'S222 NETWORK (no MAC addresses)'; Get-NetAdapter -IncludeHidden | Select InterfaceDescription,Status,LinkSpeed | Format-Table -AutoSize; Write-Output ('S222 FREE_BYTES='+[long](Get-PSDrive C).Free); Write-Output 'S222 QUERY COMPLETE'"
if errorlevel 1 exit /b %errorlevel%
D:\CPUSTRES.EXE
if errorlevel 1 exit /b %errorlevel%
D:\DISKREAD.EXE
exit /b %errorlevel%
