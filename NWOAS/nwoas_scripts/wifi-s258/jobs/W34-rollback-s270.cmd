@echo off
echo S270 W34 ROLLBACK to s258r: delete only the nwoa4378 1.0.1.10 (s270) driver package, then s258r 1.0.1.9
powershell -NoProfile -Command "$o=(pnputil /enum-drivers) -join \"`n\"; $bl=$o -split \"`n`n\" | Where-Object { $_ -match 'nwoa4378.inf' }; $r=@($bl | Where-Object { $_ -match '1\.0\.1\.10(\s|$)' }); $q=@($bl | Where-Object { $_ -match '1\.0\.1\.9(\s|$)' }); 'W34 PKG s270='+$r.Count+' s258r='+$q.Count; if($r.Count -ne 1 -or $q.Count -lt 1){ 'W34 ABORT store not as expected'; exit 2 }; if($r[0] -match '(oem\d+\.inf)'){ $m=$Matches[1]; 'W34 DELETE '+$m; pnputil /delete-driver $m /uninstall; 'W34 DELETE EXIT '+$LASTEXITCODE } else { 'W34 ABORT no oem name'; exit 2 }"
if errorlevel 2 (echo S270 W34 ABORT & exit /b 1)
pnputil /scan-devices
ping -n 31 127.0.0.1 >nul
powershell -NoProfile -Command "Get-PnpDeviceProperty -InstanceId 'ACPI\NWOA4378\0' -KeyName DEVPKEY_Device_DriverInfPath,DEVPKEY_Device_DriverVersion -ErrorAction SilentlyContinue | ForEach-Object { 'PROP '+$_.KeyName+' = '+$_.Data }"
powershell -NoProfile -Command "$v=(Get-PnpDeviceProperty -InstanceId 'ACPI\NWOA4378\0' -KeyName DEVPKEY_Device_DriverVersion -ErrorAction SilentlyContinue).Data; if($v -ne '1.0.1.9' -and (Test-Path 'C:\NWOAS\drop\wifi-j274-20261119-1056-s258r\nwoa4378.inf')){ 'W34 REINSTALL s258r from drop folder (version now '+$v+')'; pnputil /add-driver 'C:\NWOAS\drop\wifi-j274-20261119-1056-s258r\nwoa4378.inf' /install; 'W34 PNPUTIL EXIT '+$LASTEXITCODE }"
ping -n 21 127.0.0.1 >nul
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaFwVersion
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaStage
echo S270 W34 DONE
exit /b 0
