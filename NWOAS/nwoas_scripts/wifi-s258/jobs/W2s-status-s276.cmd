@echo off
echo S258 W2 WIFI STATUS
powershell -NoProfile -Command "$d=Get-PnpDevice -InstanceId 'ACPI\NWOA4378\0' -ErrorAction SilentlyContinue; if($d){ 'DEV '+$d.InstanceId+' status='+$d.Status+' problem='+$d.Problem+' class='+$d.Class+' name='+$d.FriendlyName; Get-PnpDeviceProperty -InstanceId 'ACPI\NWOA4378\0' -KeyName DEVPKEY_Device_DriverInfPath,DEVPKEY_Device_ProblemStatus -ErrorAction SilentlyContinue | ForEach-Object { 'PROP '+$_.KeyName+' = '+$_.Data } } else { 'DEV none' }"
powershell -NoProfile -Command "Get-NetAdapter -ErrorAction SilentlyContinue | ForEach-Object { 'NIC '+$_.Name+' / '+$_.InterfaceDescription+' / '+$_.Status+' / '+$_.MediaType }; Get-Service WlanSvc | ForEach-Object { 'SVC '+$_.Name+' '+$_.Status }"
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" | findstr /v /c:"NwoaSnap"
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" | findstr /r /c:"NwoaSnap[0-9]  *REG_SZ"
netsh wlan show interfaces
netsh wlan show networks mode=bssid
echo S258 W2 DONE
exit /b 0
