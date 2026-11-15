@echo off
echo S231 W1 INSTALLED WINDOWS FIRST-BOOT READ-ONLY STATE
ver
echo CPUS %NUMBER_OF_PROCESSORS% ARCH %PROCESSOR_ARCHITECTURE% SYSTEMDRIVE %SystemDrive%
reg query HKLM\SYSTEM\Setup /v SystemSetupInProgress
reg query HKLM\SYSTEM\Setup /v OOBEInProgress
reg query HKLM\SYSTEM\Setup\Status /s
schtasks.exe /Query /TN "NWOAS Agent"
for %%d in (C D E F S) do if exist %%d:\nul vol %%d:
echo S231 W1 COMPLETE
exit /b 0
