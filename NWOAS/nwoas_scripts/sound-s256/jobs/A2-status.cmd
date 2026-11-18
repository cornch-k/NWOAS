@echo off
echo S256 A2 AUDIO STATUS
powershell -NoProfile -Command "$d=Get-PnpDevice -InstanceId 'ACPI\NWAU0001\0'; 'DEV '+$d.Status+' problem='+$d.Problem+' name='+$d.FriendlyName; Get-PnpDeviceProperty -InstanceId 'ACPI\NWAU0001\0' -KeyName DEVPKEY_Device_DriverInfPath,DEVPKEY_Device_ProblemStatus -ErrorAction SilentlyContinue | ForEach-Object { 'PROP '+$_.KeyName+' = '+$_.Data }"
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWAU0001\0\Device Parameters"
powershell -NoProfile -Command "Get-PnpDevice -Class AudioEndpoint -ErrorAction SilentlyContinue | ForEach-Object { 'EP '+$_.Status+' '+$_.FriendlyName+' '+$_.InstanceId }; Get-PnpDevice -Class MEDIA -ErrorAction SilentlyContinue | ForEach-Object { 'MEDIA '+$_.Status+' '+$_.FriendlyName+' '+$_.InstanceId }; Get-Service Audiosrv,AudioEndpointBuilder | ForEach-Object { 'SVC '+$_.Name+' '+$_.Status }"
echo S256 A2 DONE
exit /b 0
