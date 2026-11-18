@echo off
echo S261 B3 BT SCAN (Windows device enumeration of nearby BR/EDR + LE devices, names masked)
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtCntEvt
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtCntAclRx
%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; try { Add-Type -AssemblyName System.Runtime.WindowsRuntime; $m=([System.WindowsRuntimeSystemExtensions].GetMethods()|Where-Object{$_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'})[0]; [void][Windows.Devices.Enumeration.DeviceInformation,Windows.Devices.Enumeration,ContentType=WindowsRuntime]; $br='{e0cbf06c-cd8b-4647-bb8a-263b43f0f974}'; $le='{bb7bb05e-5972-42b5-94fc-76eaa7084d49}'; $q='System.Devices.Aep.ProtocolId:=\"'+$br+'\" OR System.Devices.Aep.ProtocolId:=\"'+$le+'\"'; $p=[string[]]@('System.Devices.Aep.ProtocolId','System.Devices.Aep.IsPaired'); $op=[Windows.Devices.Enumeration.DeviceInformation]::FindAllAsync($q,$p,[Windows.Devices.Enumeration.DeviceInformationKind]::AssociationEndpoint); $t=$m.MakeGenericMethod([Windows.Devices.Enumeration.DeviceInformationCollection]).Invoke($null,@($op)); [void]$t.Wait(40000); $d=@($t.Result); $nb=0; $nl=0; foreach($x in $d){ $pid2=[string]$x.Properties['System.Devices.Aep.ProtocolId']; if($pid2 -match 'e0cbf06c'){$nb++}else{$nl++}; $n=[string]$x.Name; $mask=if($n){$n.Substring(0,[Math]::Min(2,$n.Length))+'*len'+$n.Length}else{'(no name)'}; 'SEEN '+$(if($pid2 -match 'e0cbf06c'){'BR/EDR'}else{'LE'})+' paired='+$x.Properties['System.Devices.Aep.IsPaired']+' name='+$mask }; 'SCAN FOUND total='+$d.Count+' bredr='+$nb+' le='+$nl } catch { 'SCAN FAILED '+$_.Exception.Message }"
ping -n 4 127.0.0.1 >nul
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtCntEvt
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtCntFix
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtDead
echo S261 B3 DONE
exit /b 0
