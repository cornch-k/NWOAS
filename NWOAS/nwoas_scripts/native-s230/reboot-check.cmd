@echo off
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; Get-CimInstance Win32_ComputerSystem | Select TotalPhysicalMemory,NumberOfLogicalProcessors | Format-List; $p='C:\ProgramData\NWOAS\IO195-s230-first.DAT'; if((Get-Item -LiteralPath $p).Length -ne 268435456 -or (Get-FileHash -LiteralPath $p).Hash -ne 'EB11897202F621134A7EC3C737C4AC3FBEEE61EF69FAA50D4B9469D8998538D6'){throw 'Post-reboot test archive mismatch'}; Write-Output 'S230 PERSISTED TEST FILE PASS'; Get-CimInstance Win32_PnPEntity | Where-Object {$_.ConfigManagerErrorCode -ne 0} | Select Name,PNPClass,ConfigManagerErrorCode | Format-Table -AutoSize; Get-CimInstance Win32_VideoController | Select Name,DriverVersion,CurrentHorizontalResolution,CurrentVerticalResolution | Format-List; Write-Output ('S230 FREE_BYTES='+[long](Get-PSDrive C).Free)"
if errorlevel 1 exit /b %errorlevel%
D:\CPUSTRES.EXE
if errorlevel 1 exit /b %errorlevel%
D:\DISKREAD.EXE
exit /b %errorlevel%
