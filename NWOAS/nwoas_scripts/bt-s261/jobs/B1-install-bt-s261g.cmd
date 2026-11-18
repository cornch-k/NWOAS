@echo off
echo S261 B1 INSTALL BT bt-j274-20261118-1849-s261g (ACPI\NWOA5F69, nwbt4378.sys BthX transport, StopAfter=1)
cd /d C:\NWOAS\drop\bt-j274-20261118-1849-s261g
if errorlevel 1 (echo S261 B1 FAIL no package dir & exit /b 1)
rem remove bring-up test values left from earlier runs: the driver production defaults apply
for %%v in (StopAfter DoFlr PollIntervalMs FixQuirks SetBdaddr SendCal SendPtb PokeAwake MsiToRam BootTimeoutMs StepTimeoutMs HciTimeoutMs CreatePdo AutoRestart FirmwareDir NwbtAutoRestarts NwbtAutoRestartAt) do reg delete "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v %%v /f >nul 2>&1
rem first load is the bisect start: control page only (B5-stopafter-N walks on, B5-stopafter-0 = full)
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v StopAfter /t REG_DWORD /d 1 /f
pnputil /add-driver nwbt4378.inf /install
echo S261 B1 PNPUTIL EXIT %ERRORLEVEL%
ping -n 46 127.0.0.1 >nul
%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe -NoProfile -NonInteractive -Command "$ErrorActionPreference='SilentlyContinue'; $ds=@(Get-PnpDevice | Where-Object { $_.InstanceId -like 'ACPI\NWOA5F69\*' -or $_.InstanceId -like 'NWBT\BCM4378BTH\*' -or $_.InstanceId -like 'ACPI\NWOA4378\*' -or $_.Class -eq 'Bluetooth' }); if (-not ($ds | Where-Object { $_.InstanceId -like 'ACPI\NWOA5F69\*' })) { 'BT ABSENT (no ACPI\NWOA5F69: _STA=0 or not the S261 payload)' }; foreach ($d in $ds) { 'DEV '+$d.InstanceId+' status='+$d.Status+' problem='+$d.Problem+' present='+$d.Present+' class='+$d.Class+' name='+$d.FriendlyName }"
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters"
%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; try { Add-Type -AssemblyName System.Runtime.WindowsRuntime; $m=([System.WindowsRuntimeSystemExtensions].GetMethods()|Where-Object{$_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'})[0]; [void][Windows.Devices.Radios.Radio,Windows.System.Devices,ContentType=WindowsRuntime]; $t=$m.MakeGenericMethod([System.Collections.Generic.IReadOnlyList[Windows.Devices.Radios.Radio]]).Invoke($null,@([Windows.Devices.Radios.Radio]::GetRadiosAsync())); [void]$t.Wait(10000); foreach($r in $t.Result){ 'RADIO kind='+$r.Kind+' state='+$r.State+' name='+$r.Name } ; 'RADIOS '+@($t.Result).Count } catch { 'RADIO QUERY FAILED '+$_.Exception.Message }"
echo S261 B1 DONE
exit /b 0
