@echo off
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; $p='C:\ProgramData\NWOAS\IO195.DAT'; $d='C:\ProgramData\NWOAS\IO195-s223-tail.DAT'; $f=Get-Item -LiteralPath $p; if($f.Length -ne 268435456){throw 'S195 unexpected size'}; if(Test-Path $d){throw 'S195 archive exists'}; $h=Get-FileHash -LiteralPath $p; $h | Format-List; if($h.Hash -ne 'EB11897202F621134A7EC3C737C4AC3FBEEE61EF69FAA50D4B9469D8998538D6'){throw 'pattern hash mismatch'}; Move-Item -LiteralPath $p -Destination $d; Get-Item -LiteralPath $d | Select Name,Length | Format-List"
if errorlevel 1 exit /b %errorlevel%
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; Write-Output ('S222 UTC='+[DateTime]::UtcNow.ToString('o')); Write-Output 'S222 CPU metadata (not measured clock)'; Get-CimInstance Win32_Processor | Select Name,NumberOfCores,NumberOfLogicalProcessors,MaxClockSpeed,CurrentClockSpeed | Format-List; Write-Output 'S222 MEMORY'; Get-CimInstance Win32_ComputerSystem | Select TotalPhysicalMemory,NumberOfLogicalProcessors | Format-List; Write-Output 'S222 DEVICE PROBLEMS (no instance IDs)'; Get-CimInstance Win32_PnPEntity | Where-Object {$_.ConfigManagerErrorCode -ne 0} | Select Name,PNPClass,ConfigManagerErrorCode | Format-Table -AutoSize; Write-Output 'S222 NETWORK (no MAC addresses)'; Get-NetAdapter -IncludeHidden | Select InterfaceDescription,Status,LinkSpeed | Format-Table -AutoSize; Write-Output ('S222 FREE_BYTES='+[long](Get-PSDrive C).Free); Write-Output 'S222 QUERY COMPLETE'"
if errorlevel 1 exit /b %errorlevel%
D:\CPUSTRES.EXE
if errorlevel 1 exit /b %errorlevel%
D:\DISKREAD.EXE
exit /b %errorlevel%
