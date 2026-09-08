@echo off
set "TOOLS="
for %%d in (C D E F G H I J) do if exist %%d:\NWESP.EXE set "TOOLS=%%d:"
if not defined TOOLS exit /b 20
%TOOLS%\NWGUARD.EXE C: identify
if errorlevel 1 exit /b 21
%TOOLS%\NWESP.EXE S: identify
if errorlevel 1 exit /b 22
echo S128 THREE BOOT MANAGER FINGERPRINTS - ZERO EXPECTATION IS A DIAGNOSTIC SENTINEL
for %%f in (C:\Windows\Boot\EFI\bootmgfw.efi S:\EFI\Microsoft\Boot\bootmgfw.efi S:\EFI\Boot\BOOTAA64.EFI) do call :hash %%f
bcdedit /store S:\EFI\Microsoft\Boot\BCD /enum {default}
echo S128 INSTALLED BOOT STATE
reg load HKLM\NWOAS_OFFLINE C:\Windows\System32\config\SYSTEM
if errorlevel 1 exit /b 23
reg query HKLM\NWOAS_OFFLINE\Setup /v CmdLine
reg query HKLM\NWOAS_OFFLINE\Setup /v SetupType
reg query HKLM\NWOAS_OFFLINE\Setup /v SystemSetupInProgress
reg unload HKLM\NWOAS_OFFLINE
if errorlevel 1 exit /b 24
%TOOLS%\NWVFLUSH.EXE C: flush
if errorlevel 1 exit /b 25
echo S128 BOOT PREPARATION INSPECTION FINISHED
exit /b 0
:hash
%TOOLS%\NWREAD.EXE %1 0000000000000000000000000000000000000000000000000000000000000000 direct
exit /b 0
