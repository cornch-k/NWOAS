@echo off
echo S277 W43 DUMP NwoaSnap5
set "K=HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters"
reg query "%K%" | findstr /b /c:"    NwoaSnap5"
echo S277 W43 DONE
exit /b 0
