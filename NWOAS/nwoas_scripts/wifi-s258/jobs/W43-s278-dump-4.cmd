@echo off
echo S278 W43 DUMP NwoaSnap4
set "K=HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters"
reg query "%K%" | findstr /b /c:"    NwoaSnap4"
echo S278 W43 DONE
exit /b 0
