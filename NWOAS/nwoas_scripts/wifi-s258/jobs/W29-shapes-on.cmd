@echo off
echo S258 W29 ShapeCycle=1 + TestConnScanSec=120 (s258q: test-scan shapes 0..5 rotated, one per test-scan verdict); RESTART ACPI\NWOA4378\0
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v ShapeCycle /t REG_DWORD /d 1 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v TestConnScanSec /t REG_DWORD /d 120 /f
pnputil /restart-device "ACPI\NWOA4378\0"
echo S258 W29 RESTART EXIT %ERRORLEVEL%
ping -n 31 127.0.0.1 >nul
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaStage
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaCfgS258q
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaShape
echo S258 W29 DONE
exit /b 0
