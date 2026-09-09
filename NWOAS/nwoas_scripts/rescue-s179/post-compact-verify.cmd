@echo off
D:\CPUSTRES.EXE
if errorlevel 1 exit /b %errorlevel%
D:\DISKREAD.EXE
if errorlevel 1 exit /b %errorlevel%
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; Get-FileHash -LiteralPath 'C:\Windows\System32\ntoskrnl.exe','C:\NWOAS-BENCH\Cinebench2026\Cinebench.exe' -Algorithm SHA256 | Format-List; Get-CimInstance Win32_LogicalDisk -Filter 'DeviceID=''C:''' | Select Size,FreeSpace | Format-List"
exit /b %errorlevel%
