@echo off
echo S258 W25 ScanFlowCheck=3 (watch and record only, no post-scan fix) + TestConnScanSec=300; RESTART ACPI\NWOA4378\0
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v ScanFlowCheck /t REG_DWORD /d 3 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v TestConnScanSec /t REG_DWORD /d 300 /f
pnputil /restart-device "ACPI\NWOA4378\0"
echo S258 W25 RESTART EXIT %ERRORLEVEL%
ping -n 31 127.0.0.1 >nul
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaStage
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaCfgS258o
echo S258 W25 DONE
exit /b 0
