@echo off
echo S261 B9 UNINSTALL nwbt4378 (all published copies) + remove stale BT child devnodes
%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe -NoProfile -NonInteractive -Command "$o=(pnputil /enum-drivers) -join \"`n\"; $blocks=$o -split \"`n`n\"; foreach($b in $blocks){ if($b -match 'nwbt4378.inf'){ if($b -match '(oem\d+\.inf)'){ $m=$Matches[1]; 'DELETE '+$m; pnputil /delete-driver $m /uninstall /force } } }"
%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe -NoProfile -NonInteractive -Command "Get-PnpDevice -ErrorAction SilentlyContinue | Where-Object { $_.InstanceId -like 'NWBT\BCM4378BTH\*' -and -not $_.Present } | ForEach-Object { 'REMOVE '+$_.InstanceId; pnputil /remove-device $_.InstanceId }"
echo S261 B9 DONE
exit /b 0
