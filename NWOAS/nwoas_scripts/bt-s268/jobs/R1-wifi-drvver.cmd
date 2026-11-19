@echo off
rem S268R R1 (read only): installed Wi-Fi driver version / INF / firmware string of ACPI\NWOA4378\0.
echo S268R R1 WIFI DRIVER VERSION
powershell -NoProfile -NonInteractive -Command "$i='ACPI\NWOA4378\0'; $d=Get-PnpDevice -InstanceId $i -ErrorAction SilentlyContinue; if(-not $d){ 'R1 DEV none'; exit }; 'R1 DEV status='+$d.Status+' problem='+$d.Problem; foreach($k in 'DEVPKEY_Device_DriverVersion','DEVPKEY_Device_DriverInfPath','DEVPKEY_Device_DriverDate','DEVPKEY_Device_DriverProvider'){ $p=Get-PnpDeviceProperty -InstanceId $i -KeyName $k -ErrorAction SilentlyContinue; 'R1 '+$k+' = '+$(if($p){[string]$p.Data}else{'n/a'}) }"
for %%v in (NwoaFwVersion NwoaDriverBuild NwoaCfgS258o) do reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v %%v 2>nul | findstr /c:"Nwoa"
echo S268R R1 DONE
exit /b 0
