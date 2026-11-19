@echo off
echo S274 W1 INSTALL WIFI wifi-j274-20261120-0034-s274fw (ACPI\NWOA4378, s274 nwoa4378.sys + INF 1.0.1.14: FwSnap + windows, macOS 13.5 Wi-Fi firmware 18.20.383)
cd /d C:\NWOAS\drop\wifi-j274-20261120-0034-s274fw
if errorlevel 1 (echo S274 W1 FAIL no package dir & exit /b 1)
rem in-place update (same device instance and NIC: saved Wi-Fi profiles stay); remove test / switch values (defaults, s258q: 45 values; s258r: + FirmwareDir so the package firmware is read)
set "K=HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters"
for %%v in (StopAfter SettleMs AccessDelayUs PostWriteRead VerifyFirmware DmaMode LinkUpAtStart UseInterrupt PollIntervalMs PollIdleMs ScanPassive RxFrameFormat Mpc PowerSave ClearNoSnoop FwRoamOff) do reg delete "%K%" /v %%v /f >nul 2>&1
for %%v in (LinkQualityUpdates RoamStay StatsReply ScanReportConnectedOnly NoDisassocIndOnHostDisconnect BssAgeInfo LinkInfoReal ReportWmm ConnectedScan PmKickAfterScan TxStallMs TxStallRecover MbPoll ConsoleLive DiagEvents ScanFlowCheck ScanFlowProbeMs ScanTxHold TestConnScanSec) do reg delete "%K%" /v %%v /f >nul 2>&1
for %%v in (ScanPm ScanChanList FlowResume InitIovarSet CandidateCycle) do reg delete "%K%" /v %%v /f >nul 2>&1
for %%v in (TestScanShape ShapeCycle ShapeChunkN ShapeChunkGapMs ShapeLongMs FirmwareDir FlowTid MbAck FlowSpare DbLegacy DbMagic FwSnap) do reg delete "%K%" /v %%v /f >nul 2>&1
pnputil /add-driver nwoa4378.inf /install
echo S274 W1 PNPUTIL EXIT %ERRORLEVEL%
ping -n 31 127.0.0.1 >nul
powershell -NoProfile -Command "$d=Get-PnpDevice -InstanceId 'ACPI\NWOA4378\0' -ErrorAction SilentlyContinue; if($d){ 'DEV '+$d.InstanceId+' status='+$d.Status+' problem='+$d.Problem+' class='+$d.Class+' name='+$d.FriendlyName; Get-PnpDeviceProperty -InstanceId 'ACPI\NWOA4378\0' -KeyName DEVPKEY_Device_DriverInfPath,DEVPKEY_Device_DriverVersion,DEVPKEY_Device_ProblemStatus -ErrorAction SilentlyContinue | ForEach-Object { 'PROP '+$_.KeyName+' = '+$_.Data } } else { 'DEV none' }"
reg query "%K%" | findstr /v /c:"NwoaSnap"
netsh wlan show interfaces
echo S274 W1 DONE
exit /b 0
