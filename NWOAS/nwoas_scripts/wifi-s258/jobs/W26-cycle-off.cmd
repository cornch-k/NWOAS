@echo off
echo S258 W26 CandidateCycle and TestConnScanSec removed (off at the next device start, no restart now)
reg delete "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v CandidateCycle /f
reg delete "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v TestConnScanSec /f
echo S258 W26 values removed; takes effect at the next device start
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaCfgS258p
echo S258 W26 DONE
exit /b 0
