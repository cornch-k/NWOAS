@echo off
echo S258 W14 RxProtectedBit=2: every frame once the PTK is installed; RESTART ACPI\NWOA4378\0
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v RxProtectedBit /t REG_DWORD /d 2 /f
pnputil /restart-device "ACPI\NWOA4378\0"
echo S258 W14 RESTART EXIT %ERRORLEVEL%
ping -n 31 127.0.0.1 >nul
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaStage
echo S258 W14 DONE
exit /b 0
