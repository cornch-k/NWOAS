@echo off
echo S231 J13 ROBOCOPY install.swm, VERIFY BOTH, CLEAN BAD COPIES, DISM FROM SSD
robocopy.exe E:\sources C:\S231RC install.swm /J /R:0 /W:0 /NP /NJH
if errorlevel 8 exit /b 91
D:\NWREAD.EXE C:\S231RC\install.swm 9b827189c3cbf61fd1c8f2c2bed2888cd3b32113aebf8d375d1ca2c72ba94dfa direct
if errorlevel 1 exit /b 92
D:\NWREAD.EXE C:\S231RC\install2.swm c19b48920f6232d4f0cc6d9617d4ba493d6a56182e5c44dcf97b36487982f0f4 direct
if errorlevel 1 exit /b 93
D:\NWREAD.EXE C:\S231RC\install.swm 9b827189c3cbf61fd1c8f2c2bed2888cd3b32113aebf8d375d1ca2c72ba94dfa buffered
if errorlevel 1 exit /b 94
D:\NWREAD.EXE C:\S231RC\install2.swm c19b48920f6232d4f0cc6d9617d4ba493d6a56182e5c44dcf97b36487982f0f4 buffered
if errorlevel 1 exit /b 95
echo SOURCE VERIFIED ON SSD
rd /s /q C:\S231SRC
rd /s /q C:\S231XJ
if exist C:\S231FC.TXT del C:\S231FC.TXT
if exist C:\Windows exit /b 96
md C:\S231SCRATCH
echo S231 DISM APPLY BEGIN
dism.exe /English /Apply-Image /ImageFile:C:\S231RC\install.swm /SWMFile:C:\S231RC\install*.swm /Index:2 /ApplyDir:C:\ /CheckIntegrity /Verify /ScratchDir:C:\S231SCRATCH /LogPath:X:\S231-DISM2.LOG
set "DISMRC=%errorlevel%"
echo S231 DISM EXIT %DISMRC%
if not "%DISMRC%"=="0" exit /b %DISMRC%
if not exist C:\Windows\System32\ntoskrnl.exe exit /b 61
if not exist C:\Windows\System32\winload.efi exit /b 62
if not exist C:\Windows\System32\config\SYSTEM exit /b 63
rd /s /q C:\S231SCRATCH
dir C:\ /a
echo S231 J13 IMAGE APPLY PASS - INSTALLED OS BOOT UNTESTED
exit /b 0
