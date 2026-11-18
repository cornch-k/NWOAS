@echo off
echo S258 W6 BACKPLANE MODE settle-only: SettleMs=1000 AccessDelayUs=0 PostWriteRead=0 (StopAfter unchanged), RESTART ACPI\NWOA4378\0
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v SettleMs /t REG_DWORD /d 1000 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v AccessDelayUs /t REG_DWORD /d 0 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v PostWriteRead /t REG_DWORD /d 0 /f
pnputil /restart-device "ACPI\NWOA4378\0"
echo S258 W6 RESTART EXIT %ERRORLEVEL%
ping -n 21 127.0.0.1 >nul
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters"
echo S258 W6 DONE
exit /b 0
