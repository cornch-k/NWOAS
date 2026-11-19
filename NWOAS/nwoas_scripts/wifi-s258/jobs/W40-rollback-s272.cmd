@echo off
echo S272 W40 ROLLBACK to s271: delete only the nwoa4378 1.0.1.12 (s272) driver package, then s271 1.0.1.11
powershell -NoProfile -Command "$o=(pnputil /enum-drivers) -join \"`n\"; $bl=$o -split \"`n`n\" | Where-Object { $_ -match 'nwoa4378.inf' }; $r=@($bl | Where-Object { $_ -match '1\.0\.1\.12(\s|$)' }); $q=@($bl | Where-Object { $_ -match '1\.0\.1\.11(\s|$)' }); 'W40 PKG s272='+$r.Count+' s271='+$q.Count; if($r.Count -ne 1 -or $q.Count -lt 1){ 'W40 ABORT store not as expected'; exit 2 }; if($r[0] -match '(oem\d+\.inf)'){ $m=$Matches[1]; 'W40 DELETE '+$m; pnputil /delete-driver $m /uninstall; 'W40 DELETE EXIT '+$LASTEXITCODE } else { 'W40 ABORT no oem name'; exit 2 }"
if errorlevel 2 (echo S272 W40 ABORT & exit /b 1)
pnputil /scan-devices
ping -n 31 127.0.0.1 >nul
powershell -NoProfile -Command "Get-PnpDeviceProperty -InstanceId 'ACPI\NWOA4378\0' -KeyName DEVPKEY_Device_DriverInfPath,DEVPKEY_Device_DriverVersion -ErrorAction SilentlyContinue | ForEach-Object { 'PROP '+$_.KeyName+' = '+$_.Data }"
powershell -NoProfile -Command "$v=(Get-PnpDeviceProperty -InstanceId 'ACPI\NWOA4378\0' -KeyName DEVPKEY_Device_DriverVersion -ErrorAction SilentlyContinue).Data; if($v -ne '1.0.1.11' -and (Test-Path 'C:\NWOAS\drop\wifi-j274-20261119-2206-s271\nwoa4378.inf')){ 'W40 REINSTALL s271 from drop folder (version now '+$v+')'; pnputil /add-driver 'C:\NWOAS\drop\wifi-j274-20261119-2206-s271\nwoa4378.inf' /install; 'W40 PNPUTIL EXIT '+$LASTEXITCODE }"
ping -n 21 127.0.0.1 >nul
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaFwVersion
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaStage
echo S272 W40 DONE
exit /b 0
