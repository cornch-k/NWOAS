@echo off
set "TOOLS="
for %%d in (C D E F G H I J) do if exist %%d:\NWESP.EXE set "TOOLS=%%d:"
if not defined TOOLS exit /b 20
%TOOLS%\NWGUARD.EXE C: identify
if errorlevel 1 exit /b 21
%TOOLS%\NWESP.EXE S: ready
if errorlevel 1 exit /b 22
if not exist C:\S124-APPLY-PASS.TXT exit /b 23
if not exist C:\Windows\System32\winload.efi exit /b 24
if exist S:\EFI\Microsoft\Boot\BCD exit /b 30
echo S128 CREATE UEFI BOOT FILES ON VERIFIED MINI ESP
bcdboot C:\Windows /s S: /f UEFI /v
set "RC=%ERRORLEVEL%"
echo S128 BCDBOOT EXIT %RC%
if not "%RC%"=="0" exit /b 25
if not exist S:\EFI\Microsoft\Boot\BCD exit /b 26
if not exist S:\EFI\Microsoft\Boot\bootmgfw.efi exit /b 27
if not exist S:\EFI\Boot md S:\EFI\Boot
if not exist S:\EFI\Boot\BOOTAA64.EFI copy /y S:\EFI\Microsoft\Boot\bootmgfw.efi S:\EFI\Boot\BOOTAA64.EFI
if not exist S:\EFI\Boot\BOOTAA64.EFI exit /b 28
bcdedit /store S:\EFI\Microsoft\Boot\BCD /enum all
if errorlevel 1 exit /b 29
%TOOLS%\NWESP.EXE S: flush
if errorlevel 1 exit /b 31
%TOOLS%\NWVFLUSH.EXE C: flush
if errorlevel 1 exit /b 32
echo S128 UEFI BOOT FILES CREATED AND FLUSHED - BOOT UNTESTED
exit /b 0
