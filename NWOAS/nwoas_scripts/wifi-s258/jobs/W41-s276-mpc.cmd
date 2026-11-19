@echo off
echo S276 W41 knob mpc: Mpc=1 (brcmfmac sets mpc 1, we 0), TestConnScanSec=120 (test scan every 120 s, shape 0 = leaves the channel), FwSnap=1; RESTART ACPI\NWOA4378\0
set "K=HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters"
for %%v in (Mpc FlowTid MbAck FlowSpare DbLegacy DbMagic FwSnap ShapeCycle TestScanShape CandidateCycle TestConnScanSec) do reg delete "%K%" /v %%v /f >nul 2>&1
for /f "tokens=1" %%a in ('reg query "%K%" ^| findstr /b /c:"    NwoaSnap"') do reg delete "%K%" /v %%a /f >nul 2>&1
reg delete "%K%" /v NwoaCfgS258o /f >nul 2>&1
reg delete "%K%" /v NwoaFlowState /f >nul 2>&1
reg add "%K%" /v TestConnScanSec /t REG_DWORD /d 120 /f
reg add "%K%" /v FwSnap /t REG_DWORD /d 1 /f
reg add "%K%" /v Mpc /t REG_DWORD /d 1 /f
pnputil /restart-device "ACPI\NWOA4378\0"
echo S276 W41 RESTART EXIT %ERRORLEVEL%
ping -n 31 127.0.0.1 >nul
for %%v in (NwoaStage NwoaFwVersion NwoaCfgS258o NwoaScanFlow) do reg query "%K%" /v %%v 2>nul | findstr /c:"Nwoa"
powershell -NoProfile -Command "(Get-PnpDeviceProperty -InstanceId 'ACPI\NWOA4378\0' -KeyName DEVPKEY_Device_DriverVersion -ErrorAction SilentlyContinue) | ForEach-Object { 'PROP '+$_.KeyName+' = '+$_.Data }"
echo S276 W41 DONE
exit /b 0
