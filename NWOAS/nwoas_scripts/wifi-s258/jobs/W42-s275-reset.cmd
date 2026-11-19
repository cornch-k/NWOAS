@echo off
echo S275 W42 RESET: every test value removed (defaults = s258r behaviour on 1.0.1.15), NwoaSnap values removed; RESTART ACPI\NWOA4378\0
set "K=HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters"
for %%v in (Mpc FlowTid MbAck FlowSpare DbLegacy DbMagic FwSnap ShapeCycle TestScanShape CandidateCycle TestConnScanSec) do reg delete "%K%" /v %%v /f >nul 2>&1
for /f "tokens=1" %%a in ('reg query "%K%" ^| findstr /b /c:"    NwoaSnap"') do reg delete "%K%" /v %%a /f >nul 2>&1
reg delete "%K%" /v NwoaCfgS258o /f >nul 2>&1
reg delete "%K%" /v NwoaFlowState /f >nul 2>&1
pnputil /restart-device "ACPI\NWOA4378\0"
echo S275 W42 RESTART EXIT %ERRORLEVEL%
ping -n 31 127.0.0.1 >nul
for %%v in (NwoaStage NwoaFwVersion NwoaCfgS258o NwoaScanFlow) do reg query "%K%" /v %%v 2>nul | findstr /c:"Nwoa"
powershell -NoProfile -Command "(Get-PnpDeviceProperty -InstanceId 'ACPI\NWOA4378\0' -KeyName DEVPKEY_Device_DriverVersion -ErrorAction SilentlyContinue) | ForEach-Object { 'PROP '+$_.KeyName+' = '+$_.Data }"
echo S275 W42 DONE
exit /b 0
