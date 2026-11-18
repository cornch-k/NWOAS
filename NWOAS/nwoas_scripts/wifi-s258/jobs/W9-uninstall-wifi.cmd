@echo off
echo S258 W9 UNINSTALL nwoa4378 (all published copies) + remove stale "not present" Wi-Fi NICs
powershell -NoProfile -Command "$o=(pnputil /enum-drivers) -join \"`n\"; $blocks=$o -split \"`n`n\"; foreach($b in $blocks){ if($b -match 'nwoa4378.inf'){ if($b -match '(oem\d+\.inf)'){ $m=$Matches[1]; 'DELETE '+$m; pnputil /delete-driver $m /uninstall /force } } }"
powershell -NoProfile -Command "Get-PnpDevice -Class Net -ErrorAction SilentlyContinue | Where-Object { -not $_.Present -and $_.FriendlyName -like '*BCM4378*' } | ForEach-Object { 'REMOVE '+$_.InstanceId+' / '+$_.FriendlyName; pnputil /remove-device $_.InstanceId }"
echo S258 W9 DONE
exit /b 0
