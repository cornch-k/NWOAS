@echo off
echo S258 W22 BssAgeInfo/LinkInfoReal back to the s258m defaults (1); RESTART ACPI\NWOA4378\0
reg delete "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v BssAgeInfo /f
reg delete "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v LinkInfoReal /f
pnputil /restart-device "ACPI\NWOA4378\0"
echo S258 W22 RESTART EXIT %ERRORLEVEL%
ping -n 31 127.0.0.1 >nul
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaStage
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaCfgS258m
echo S258 W22 DONE
exit /b 0
