@echo off
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; $p='C:\ProgramData\NWOAS\IO195-s229-first.DAT'; if((Get-Item -LiteralPath $p).Length -ne 268435456 -or (Get-FileHash -LiteralPath $p).Hash -ne 'EB11897202F621134A7EC3C737C4AC3FBEEE61EF69FAA50D4B9469D8998538D6'){throw 'S229 archive mismatch'}; Write-Output 'S229 PERSISTENCE PASS'"
if errorlevel 1 exit /b %errorlevel%
if exist C:\ProgramData\NWOAS\IO195.DAT exit /b 40
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; Get-CimInstance Win32_ComputerSystem | Select TotalPhysicalMemory,NumberOfLogicalProcessors | Format-List; $p='C:\NWOAS-S195\IOTEST.EXE'; if((Get-FileHash -LiteralPath $p).Hash -ne 'EFFD43FF105CB5D4EDE2D72A9F415EAC02557012F0B419254688F53F47FA2BB8'){throw 'Unexpected IOTEST hash'}"
if errorlevel 1 exit /b %errorlevel%
D:\CPUSTRES.EXE
if errorlevel 1 exit /b %errorlevel%
D:\DISKREAD.EXE
if errorlevel 1 exit /b %errorlevel%
C:\NWOAS-S195\IOTEST.EXE
exit /b %errorlevel%
