@echo off
echo S258 W24 TestConnScanSec=300 (driver-started directed scan every 300 s while connected, s258o test); RESTART ACPI\NWOA4378\0
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v TestConnScanSec /t REG_DWORD /d 300 /f
pnputil /restart-device "ACPI\NWOA4378\0"
echo S258 W24 RESTART EXIT %ERRORLEVEL%
ping -n 31 127.0.0.1 >nul
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaStage
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaCfgS258o
echo S258 W24 DONE
exit /b 0
