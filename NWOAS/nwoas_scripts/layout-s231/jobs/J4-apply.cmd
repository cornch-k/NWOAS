@echo off
echo S231 J4 DISM APPLY WINDOWS 11 PRO INDEX 2 FROM USB TO C:
if not exist E:\sources\install.swm exit /b 51
if not exist E:\sources\install2.swm exit /b 52
vol C:
if exist C:\Windows exit /b 53
md C:\S231SCRATCH
dism.exe /English /Get-WimInfo /WimFile:E:\sources\install.swm
echo S231 DISM APPLY BEGIN
dism.exe /English /Apply-Image /ImageFile:E:\sources\install.swm /SWMFile:E:\sources\install*.swm /Index:2 /ApplyDir:C:\ /CheckIntegrity /Verify /ScratchDir:C:\S231SCRATCH /LogPath:X:\S231-DISM.LOG
set "DISMRC=%errorlevel%"
echo S231 DISM EXIT %DISMRC%
if not "%DISMRC%"=="0" exit /b %DISMRC%
if not exist C:\Windows\System32\ntoskrnl.exe exit /b 61
if not exist C:\Windows\System32\winload.efi exit /b 62
if not exist C:\Windows\System32\config\SYSTEM exit /b 63
rd /s /q C:\S231SCRATCH
dir C:\ /a
echo S231 J4 IMAGE APPLY PASS - INSTALLED OS BOOT UNTESTED
exit /b 0
