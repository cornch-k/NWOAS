@echo off
echo S271 W36 RESET: FlowTid MbAck FlowSpare DbLegacy TestConnScanSec removed (all defaults = s258r behaviour on 1.0.1.11); RESTART ACPI\NWOA4378\0
set "K=HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters"
for %%v in (FlowTid MbAck FlowSpare DbLegacy TestConnScanSec ShapeCycle TestScanShape CandidateCycle) do reg delete "%K%" /v %%v /f >nul 2>&1
reg delete "%K%" /v NwoaCfgS258o /f >nul 2>&1
reg delete "%K%" /v NwoaS270 /f >nul 2>&1
pnputil /restart-device "ACPI\NWOA4378\0"
echo S271 W36 RESTART EXIT %ERRORLEVEL%
ping -n 31 127.0.0.1 >nul
for %%v in (NwoaStage NwoaCfgS258o NwoaS270) do reg query "%K%" /v %%v 2>nul | findstr /c:"Nwoa"
echo S271 W36 DONE
exit /b 0
