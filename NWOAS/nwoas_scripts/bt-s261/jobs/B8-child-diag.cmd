@echo off
echo S261 B8 CHILD DIAG (Bluetooth Radio child of NWOA5F69)
powershell -NoProfile -Command "Get-PnpDevice -Class Bluetooth -ErrorAction SilentlyContinue | ForEach-Object { 'DEV '+$_.InstanceId+' status='+$_.Status+' problem='+$_.Problem+' name='+$_.FriendlyName; Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName DEVPKEY_Device_ProblemStatus,DEVPKEY_Device_DriverInfPath,DEVPKEY_Device_Service,DEVPKEY_Device_DriverVersion -ErrorAction SilentlyContinue | ForEach-Object { '  '+$_.KeyName+' = '+$_.Data } }; Get-WinEvent -FilterHashtable @{LogName='System'; StartTime=(Get-Date).AddMinutes(-30)} -ErrorAction SilentlyContinue | Where-Object { $_.ProviderName -match 'BTH|Bluetooth|BthMini|BTHUSB|Kernel-PnP|bthport' } | Select-Object -First 25 | ForEach-Object { 'EVT '+$_.TimeCreated.ToString('HH:mm:ss')+' '+$_.ProviderName+' '+$_.Id+' '+($_.Message -replace '\s+',' ').Substring(0,[math]::Min(240,($_.Message -replace '\s+',' ').Length)) }"
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters"
echo S261 B8 DONE
exit /b 0
