@echo off
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; $ds=Get-PnpDevice -PresentOnly | Where-Object {$_.InstanceId -match 'VID_05AC|VID_3434|PNP0D1|ROOT_HUB30'}; foreach($d in $ds) { $p=Get-PnpDeviceProperty -InstanceId $d.InstanceId -KeyName DEVPKEY_Device_Parent -ErrorAction SilentlyContinue; $l=Get-PnpDeviceProperty -InstanceId $d.InstanceId -KeyName DEVPKEY_Device_LocationPaths -ErrorAction SilentlyContinue; [pscustomobject]@{Id=$d.InstanceId;Status=$d.Status;Parent=$p.Data;Location=($l.Data -join ';')} | Format-List }"
exit /b %errorlevel%
