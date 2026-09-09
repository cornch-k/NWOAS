@echo off
echo S156_USBC_RESTART_BEGIN %date% %time%
pnputil /restart-device "ACPI\PNP0D15\1"
set "NWOAS_RESTART_RC=%errorlevel%"
echo S156_USBC_RESTART_RC=%NWOAS_RESTART_RC%
exit /b %NWOAS_RESTART_RC%
