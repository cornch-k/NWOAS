@echo off
echo S258 W5 SET StopAfter=4 (0 = full bring-up) AND RESTART ACPI\NWOA4378\0
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v StopAfter /t REG_DWORD /d 4 /f
pnputil /restart-device "ACPI\NWOA4378\0"
echo S258 W5 RESTART EXIT %ERRORLEVEL%
ping -n 21 127.0.0.1 >nul
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters"
echo S258 W5 DONE
exit /b 0
