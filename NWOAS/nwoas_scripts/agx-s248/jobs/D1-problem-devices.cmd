@echo off
echo S248 D1 PROBLEM DEVICES
pnputil /enum-devices /problem /ids
echo S248 D1 ACPI AGX
pnputil /enum-devices /instanceid "ACPI\APPL0002\*" /ids
pnputil /enum-devices /deviceid "ACPI\APPL0002"
exit /b 0
