@echo off
echo S258 W30 base test scan only: TestConnScanSec=120, ShapeCycle and TestScanShape removed (shape 0 base every 120 s); RESTART ACPI\NWOA4378\0
reg delete "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v ShapeCycle /f >nul 2>&1
reg delete "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v TestScanShape /f >nul 2>&1
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v TestConnScanSec /t REG_DWORD /d 120 /f
pnputil /restart-device "ACPI\NWOA4378\0"
echo S258 W30 RESTART EXIT %ERRORLEVEL%
ping -n 31 127.0.0.1 >nul
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaStage
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaFwVersion
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaCfgS258q
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaShape
echo S258 W30 DONE
exit /b 0
