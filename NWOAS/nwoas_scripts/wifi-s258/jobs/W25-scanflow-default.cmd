@echo off
echo S258 W25 ScanFlowCheck and TestConnScanSec removed (defaults at the next device start, no restart now)
reg delete "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v ScanFlowCheck /f
reg delete "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v TestConnScanSec /f
echo S258 W25 value set; takes effect at the next device start
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaCfgS258o
echo S258 W25 DONE
exit /b 0
