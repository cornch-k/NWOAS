@echo off
echo S271 W37 ROLLBACK to s270: delete only the nwoa4378 1.0.1.11 (s271) driver package, then s270 1.0.1.10
powershell -NoProfile -Command "$o=(pnputil /enum-drivers) -join \"`n\"; $bl=$o -split \"`n`n\" | Where-Object { $_ -match 'nwoa4378.inf' }; $r=@($bl | Where-Object { $_ -match '1\.0\.1\.11(\s|$)' }); $q=@($bl | Where-Object { $_ -match '1\.0\.1\.10(\s|$)' }); 'W37 PKG s271='+$r.Count+' s270='+$q.Count; if($r.Count -ne 1 -or $q.Count -lt 1){ 'W37 ABORT store not as expected'; exit 2 }; if($r[0] -match '(oem\d+\.inf)'){ $m=$Matches[1]; 'W37 DELETE '+$m; pnputil /delete-driver $m /uninstall; 'W37 DELETE EXIT '+$LASTEXITCODE } else { 'W37 ABORT no oem name'; exit 2 }"
if errorlevel 2 (echo S271 W37 ABORT & exit /b 1)
pnputil /scan-devices
ping -n 31 127.0.0.1 >nul
powershell -NoProfile -Command "Get-PnpDeviceProperty -InstanceId 'ACPI\NWOA4378\0' -KeyName DEVPKEY_Device_DriverInfPath,DEVPKEY_Device_DriverVersion -ErrorAction SilentlyContinue | ForEach-Object { 'PROP '+$_.KeyName+' = '+$_.Data }"
powershell -NoProfile -Command "$v=(Get-PnpDeviceProperty -InstanceId 'ACPI\NWOA4378\0' -KeyName DEVPKEY_Device_DriverVersion -ErrorAction SilentlyContinue).Data; if($v -ne '1.0.1.10' -and (Test-Path 'C:\NWOAS\drop\wifi-j274-20261119-2049-s270\nwoa4378.inf')){ 'W37 REINSTALL s270 from drop folder (version now '+$v+')'; pnputil /add-driver 'C:\NWOAS\drop\wifi-j274-20261119-2049-s270\nwoa4378.inf' /install; 'W37 PNPUTIL EXIT '+$LASTEXITCODE }"
ping -n 21 127.0.0.1 >nul
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaFwVersion
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaStage
echo S271 W37 DONE
exit /b 0
