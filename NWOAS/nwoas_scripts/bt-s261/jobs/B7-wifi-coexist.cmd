@echo off
echo S261 B7 COEXIST: restart the Wi-Fi device while Bluetooth runs, then check both
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtStage
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtCntEvt
pnputil /restart-device "ACPI\NWOA4378\0"
echo S261 B7 WIFI RESTART EXIT %ERRORLEVEL%
ping -n 41 127.0.0.1 >nul
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters"
%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe -NoProfile -NonInteractive -Command "$ErrorActionPreference='SilentlyContinue'; $ds=@(Get-PnpDevice | Where-Object { $_.InstanceId -like 'ACPI\NWOA5F69\*' -or $_.InstanceId -like 'NWBT\BCM4378BTH\*' -or $_.InstanceId -like 'ACPI\NWOA4378\*' -or $_.Class -eq 'Bluetooth' }); if (-not ($ds | Where-Object { $_.InstanceId -like 'ACPI\NWOA5F69\*' })) { 'BT ABSENT (no ACPI\NWOA5F69: _STA=0 or not the S261 payload)' }; foreach ($d in $ds) { 'DEV '+$d.InstanceId+' status='+$d.Status+' problem='+$d.Problem+' present='+$d.Present+' class='+$d.Class+' name='+$d.FriendlyName }"
%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; try { Add-Type -AssemblyName System.Runtime.WindowsRuntime; $m=([System.WindowsRuntimeSystemExtensions].GetMethods()|Where-Object{$_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'})[0]; [void][Windows.Devices.Radios.Radio,Windows.System.Devices,ContentType=WindowsRuntime]; $t=$m.MakeGenericMethod([System.Collections.Generic.IReadOnlyList[Windows.Devices.Radios.Radio]]).Invoke($null,@([Windows.Devices.Radios.Radio]::GetRadiosAsync())); [void]$t.Wait(10000); foreach($r in $t.Result){ 'RADIO kind='+$r.Kind+' state='+$r.State+' name='+$r.Name } ; 'RADIOS '+@($t.Result).Count } catch { 'RADIO QUERY FAILED '+$_.Exception.Message }"
netsh wlan show interfaces | findstr /R /C:"State" /C:"SSID"
echo S261 B7 DONE
exit /b 0
