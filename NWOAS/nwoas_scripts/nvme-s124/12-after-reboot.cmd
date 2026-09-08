@echo off
set "TOOLS="
for %%d in (C D E F G H I J) do if exist %%d:\NWAGENT.EXE set "TOOLS=%%d:"
if not defined TOOLS exit /b 20
%TOOLS%\NWGUARD.EXE C: identify
if errorlevel 1 exit /b 21
echo S126 READ-ONLY POST-REBOOT INSTALL INSPECTION
if exist C:\S124-APPLY-PASS.TXT (type C:\S124-APPLY-PASS.TXT) else echo NOTE: final marker absent; inspect flushed installation independently
if not exist C:\Windows\System32\ntoskrnl.exe exit /b 22
if not exist C:\Windows\System32\winload.efi exit /b 23
if not exist C:\Windows\System32\config\SYSTEM exit /b 24
echo S126 KERNEL FINGERPRINT - ZERO EXPECTATION IS A DIAGNOSTIC SENTINEL
%TOOLS%\NWREAD.EXE C:\Windows\System32\ntoskrnl.exe 0000000000000000000000000000000000000000000000000000000000000000 direct
if errorlevel 2 exit /b 25
echo S126 LOADER FINGERPRINT
%TOOLS%\NWREAD.EXE C:\Windows\System32\winload.efi 0000000000000000000000000000000000000000000000000000000000000000 direct
if errorlevel 2 exit /b 26
echo S126 CURRENT PE BOOT CONFIGURATION
bcdedit /enum all
echo S126 INSTALLED EDITION
dism /English /Image:C:\ /Get-CurrentEdition /LogPath:X:\S126-EDITION.LOG
if errorlevel 1 exit /b 27
echo S126 INSTALLED ROOT
dir C:\ /a
echo S126 POST-REBOOT INSPECTION COMPLETE - OS BOOT UNTESTED
exit /b 0
