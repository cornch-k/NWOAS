@echo off
echo S278 W44 ROLLBACK to s275: delete only the nwoa4378 1.0.1.18 (s278) driver package, then s275 1.0.1.15
powershell -NoProfile -Command "$o=(pnputil /enum-drivers) -join \"`n\"; $bl=$o -split \"`n`n\" | Where-Object { $_ -match 'nwoa4378.inf' }; $r=@($bl | Where-Object { $_ -match '1\.0\.1\.18(\s|$)' }); $q=@($bl | Where-Object { $_ -match '1\.0\.1\.15(\s|$)' }); 'W44 PKG s278='+$r.Count+' s275='+$q.Count; if($r.Count -ne 1 -or $q.Count -lt 1){ 'W44 ABORT store not as expected'; exit 2 }; if($r[0] -match '(oem\d+\.inf)'){ $m=$Matches[1]; 'W44 DELETE '+$m; pnputil /delete-driver $m /uninstall; 'W44 DELETE EXIT '+$LASTEXITCODE } else { 'W44 ABORT no oem name'; exit 2 }"
if errorlevel 2 (echo S278 W44 ABORT & exit /b 1)
pnputil /scan-devices
ping -n 31 127.0.0.1 >nul
powershell -NoProfile -Command "Get-PnpDeviceProperty -InstanceId 'ACPI\NWOA4378\0' -KeyName DEVPKEY_Device_DriverInfPath,DEVPKEY_Device_DriverVersion -ErrorAction SilentlyContinue | ForEach-Object { 'PROP '+$_.KeyName+' = '+$_.Data }"
powershell -NoProfile -Command "$v=(Get-PnpDeviceProperty -InstanceId 'ACPI\NWOA4378\0' -KeyName DEVPKEY_Device_DriverVersion -ErrorAction SilentlyContinue).Data; if($v -ne '1.0.1.15' -and (Test-Path 'C:\NWOAS\drop\wifi-j274-20261120-0127-s275fw\nwoa4378.inf')){ 'W44 REINSTALL s275 from drop folder (version now '+$v+')'; pnputil /add-driver 'C:\NWOAS\drop\wifi-j274-20261120-0127-s275fw\nwoa4378.inf' /install; 'W44 PNPUTIL EXIT '+$LASTEXITCODE }"
ping -n 21 127.0.0.1 >nul
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaFwVersion
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaStage
echo S278 W44 DONE
exit /b 0
