@echo off
echo S270 W32 knob tid1: TestConnScanSec=120 (driver test scan every 120 s, shape 0), FlowTid=1; other s270 knobs removed; RESTART ACPI\NWOA4378\0
set "K=HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters"
for %%v in (FlowTid MbAck FlowSpare ShapeCycle TestScanShape CandidateCycle) do reg delete "%K%" /v %%v /f >nul 2>&1
reg add "%K%" /v TestConnScanSec /t REG_DWORD /d 120 /f
reg add "%K%" /v FlowTid /t REG_DWORD /d 1 /f
reg delete "%K%" /v NwoaCfgS258o /f >nul 2>&1
reg delete "%K%" /v NwoaS270 /f >nul 2>&1
reg delete "%K%" /v NwoaFlowState /f >nul 2>&1
pnputil /restart-device "ACPI\NWOA4378\0"
echo S270 W32 RESTART EXIT %ERRORLEVEL%
ping -n 31 127.0.0.1 >nul
for %%v in (NwoaStage NwoaFwVersion NwoaCfgS258o NwoaS270 NwoaScanFlow) do reg query "%K%" /v %%v 2>nul | findstr /c:"Nwoa"
powershell -NoProfile -Command "(Get-PnpDeviceProperty -InstanceId 'ACPI\NWOA4378\0' -KeyName DEVPKEY_Device_DriverVersion -ErrorAction SilentlyContinue) | ForEach-Object { 'PROP '+$_.KeyName+' = '+$_.Data }"
echo S270 W32 DONE
exit /b 0
