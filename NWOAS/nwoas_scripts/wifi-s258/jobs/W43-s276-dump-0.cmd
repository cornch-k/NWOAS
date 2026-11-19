@echo off
echo S276 W43 DUMP NwoaSnap0
set "K=HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters"
reg query "%K%" | findstr /b /c:"    NwoaSnap0"
echo S276 W43 DONE
exit /b 0
