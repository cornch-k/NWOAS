@echo off
echo S258 W8 SET RxFrameFormat=1 (deliver 802.11 + LLC, NIC is Native 802.11) AND RESTART ACPI\NWOA4378\0
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v RxFrameFormat /t REG_DWORD /d 1 /f
pnputil /restart-device "ACPI\NWOA4378\0"
echo S258 W8 RESTART EXIT %ERRORLEVEL%
ping -n 21 127.0.0.1 >nul
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v RxFrameFormat
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaStage
echo S258 W8 DONE
exit /b 0
