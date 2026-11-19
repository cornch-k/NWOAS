@echo off
echo S258 W23 ConnectedScan=0 (firmware scan while connected as s258m); RESTART ACPI\NWOA4378\0
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v ConnectedScan /t REG_DWORD /d 0 /f
pnputil /restart-device "ACPI\NWOA4378\0"
echo S258 W23 RESTART EXIT %ERRORLEVEL%
ping -n 31 127.0.0.1 >nul
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaStage
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaCfgS258n
echo S258 W23 DONE
exit /b 0
