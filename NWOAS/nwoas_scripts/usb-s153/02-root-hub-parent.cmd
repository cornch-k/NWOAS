@echo off
echo === S153 ROOT HUB PARENTS BEGIN ===
powershell -NoProfile -Command "Get-PnpDevice -Class USB | Where-Object FriendlyName -eq 'USB Root Hub (USB 3.0)' | ForEach-Object { Write-Output ('HUB '+$_.InstanceId+' STATUS='+$_.Status+' PROBLEM='+$_.Problem); Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName 'DEVPKEY_Device_Parent','DEVPKEY_Device_ProblemCode','DEVPKEY_Device_ProblemStatus' | Format-Table -HideTableHeaders KeyName,Data }"
echo === S153 ROOT HUB PARENTS END ===
exit /b 0
