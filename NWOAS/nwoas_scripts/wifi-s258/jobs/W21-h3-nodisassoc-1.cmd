@echo off
echo S258 W21 NoDisassocIndOnHostDisconnect=1: H3 test, no DISASSOCIATION indication after WDI_TASK_DISCONNECT; RESTART ACPI\NWOA4378\0
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NoDisassocIndOnHostDisconnect /t REG_DWORD /d 1 /f
pnputil /restart-device "ACPI\NWOA4378\0"
echo S258 W21 RESTART EXIT %ERRORLEVEL%
ping -n 31 127.0.0.1 >nul
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaStage
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaCfgS258m
echo S258 W21 DONE
exit /b 0
