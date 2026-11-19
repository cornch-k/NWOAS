@echo off
echo S258 W29 ShapeCycle and TestConnScanSec removed (off at the next device start, no restart now)
reg delete "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v ShapeCycle /f
reg delete "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v TestConnScanSec /f
echo S258 W29 values removed; takes effect at the next device start
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaCfgS258q
echo S258 W29 DONE
exit /b 0
