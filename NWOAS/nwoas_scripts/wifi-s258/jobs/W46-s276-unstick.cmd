@echo off
echo S276 W46 UNSTICK: remove RomDump and NwoaRom0..3 (no device restart)
set "K=HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters"
reg delete "%K%" /v RomDump /f
for %%i in (0 1 2 3) do reg delete "%K%" /v NwoaRom%%i /f >nul 2>&1
reg query "%K%" /v RomDump
echo S276 W46 DONE (RomDump must be reported missing above)
exit /b 0
