@echo off
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; Get-CimInstance Win32_ComputerSystem | Select TotalPhysicalMemory,NumberOfLogicalProcessors | Format-List; $p='C:\NWOAS-S195\IOTEST.EXE'; if((Get-FileHash -LiteralPath $p).Hash -ne 'EFFD43FF105CB5D4EDE2D72A9F415EAC02557012F0B419254688F53F47FA2BB8'){throw 'Unexpected IOTEST hash'}"
if errorlevel 1 exit /b %errorlevel%
D:\CPUSTRES.EXE
if errorlevel 1 exit /b %errorlevel%
D:\DISKREAD.EXE
if errorlevel 1 exit /b %errorlevel%
C:\NWOAS-S195\IOTEST.EXE
exit /b %errorlevel%
