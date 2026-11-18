@echo off
echo S258 W7 SET LinkUpAtStart=1 AND RESTART ACPI\NWOA4378\0
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v LinkUpAtStart /t REG_DWORD /d 1 /f
pnputil /restart-device "ACPI\NWOA4378\0"
echo S258 W7 RESTART EXIT %ERRORLEVEL%
ping -n 21 127.0.0.1 >nul
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaStage
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaTxQueueCreate
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaRxQueueCreate
echo S258 W7 DONE
exit /b 0
