@echo off
echo S258 W28-restore FwRoamOff=1 UseInterrupt=0 TestConnScanSec=0 ScanTxHold=0 CandidateCycle=0; RESTART
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v FwRoamOff /t REG_DWORD /d 1 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v UseInterrupt /t REG_DWORD /d 0 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v TestConnScanSec /t REG_DWORD /d 0 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v ScanTxHold /t REG_DWORD /d 0 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v CandidateCycle /t REG_DWORD /d 0 /f
pnputil /restart-device "ACPI\NWOA4378\0"
echo RESTART EXIT %ERRORLEVEL%
ping -n 31 127.0.0.1 >nul
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaCfgS258p
echo S258 W28-restore DONE
exit /b 0
