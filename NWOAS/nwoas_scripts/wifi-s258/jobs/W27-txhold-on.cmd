@echo off
echo S258 W27 ScanTxHold=1 TestConnScanSec=120 CandidateCycle=0; RESTART ACPI\NWOA4378\0
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v ScanTxHold /t REG_DWORD /d 1 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v TestConnScanSec /t REG_DWORD /d 120 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v CandidateCycle /t REG_DWORD /d 0 /f
pnputil /restart-device "ACPI\NWOA4378\0"
echo S258 W27 RESTART EXIT %ERRORLEVEL%
ping -n 31 127.0.0.1 >nul
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaCfgS258p
echo S258 W27 DONE
exit /b 0
