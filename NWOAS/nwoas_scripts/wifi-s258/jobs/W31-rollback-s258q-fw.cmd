@echo off
echo S258 W31 ROLLBACK to s258q firmware: delete only the nwoa4378 1.0.1.9 (s258r) driver package, then s258q 1.0.1.8
powershell -NoProfile -Command "$o=(pnputil /enum-drivers) -join \"`n\"; $bl=$o -split \"`n`n\" | Where-Object { $_ -match 'nwoa4378.inf' }; $r=@($bl | Where-Object { $_ -match '1\.0\.1\.9(\s|$)' }); $q=@($bl | Where-Object { $_ -match '1\.0\.1\.8(\s|$)' }); 'W31 PKG s258r='+$r.Count+' s258q='+$q.Count; if($r.Count -ne 1 -or $q.Count -lt 1){ 'W31 ABORT store not as expected'; exit 2 }; if($r[0] -match '(oem\d+\.inf)'){ $m=$Matches[1]; 'W31 DELETE '+$m; pnputil /delete-driver $m /uninstall; 'W31 DELETE EXIT '+$LASTEXITCODE } else { 'W31 ABORT no oem name'; exit 2 }"
if errorlevel 2 (echo S258 W31 ABORT & exit /b 1)
pnputil /scan-devices
ping -n 31 127.0.0.1 >nul
powershell -NoProfile -Command "Get-PnpDeviceProperty -InstanceId 'ACPI\NWOA4378\0' -KeyName DEVPKEY_Device_DriverInfPath,DEVPKEY_Device_DriverVersion -ErrorAction SilentlyContinue | ForEach-Object { 'PROP '+$_.KeyName+' = '+$_.Data }"
powershell -NoProfile -Command "$v=(Get-PnpDeviceProperty -InstanceId 'ACPI\NWOA4378\0' -KeyName DEVPKEY_Device_DriverVersion -ErrorAction SilentlyContinue).Data; if($v -ne '1.0.1.8' -and (Test-Path 'C:\NWOAS\drop\wifi-j274-20261119-1020-s258q\nwoa4378.inf')){ 'W31 REINSTALL s258q from drop folder (version now '+$v+')'; pnputil /add-driver 'C:\NWOAS\drop\wifi-j274-20261119-1020-s258q\nwoa4378.inf' /install; 'W31 PNPUTIL EXIT '+$LASTEXITCODE }"
ping -n 21 127.0.0.1 >nul
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaFwVersion
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaStage
echo S258 W31 DONE
exit /b 0
