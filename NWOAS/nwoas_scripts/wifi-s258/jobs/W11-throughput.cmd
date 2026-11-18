@echo off
echo S258 W11 THROUGHPUT (download 50 MB from speed.cloudflare.com, 3 runs; gateway ping)
powershell -NoProfile -Command "$ProgressPreference='SilentlyContinue'; $u='https://speed.cloudflare.com/__down?bytes=52428800'; $f=Join-Path $env:TEMP 'nwoas-tp.bin'; for($i=1;$i -le 3;$i++){ try { $t=[Diagnostics.Stopwatch]::StartNew(); Invoke-WebRequest -UseBasicParsing -Uri $u -OutFile $f -TimeoutSec 180; $t.Stop(); $n=(Get-Item $f).Length; 'RUN '+$i+' bytes='+$n+' sec='+[math]::Round($t.Elapsed.TotalSeconds,2)+' Mbps='+[math]::Round($n*8/1e6/$t.Elapsed.TotalSeconds,1) } catch { 'RUN '+$i+' FAIL '+$_.Exception.Message } finally { Remove-Item $f -ErrorAction SilentlyContinue } }; $g=(Get-NetRoute -DestinationPrefix 0.0.0.0/0 -ErrorAction SilentlyContinue | Select-Object -First 1).NextHop; if($g){ Test-Connection -Count 10 -ComputerName $g -ErrorAction SilentlyContinue | Measure-Object -Property ResponseTime -Average -Maximum | ForEach-Object { 'PING '+$g+' n='+$_.Count+' avg='+[math]::Round($_.Average,1)+' max='+$_.Maximum+' ms' } }; Get-NetAdapter | Where-Object { $_.InterfaceDescription -like '*BCM4378*' -and $_.Status -eq 'Up' } | ForEach-Object { 'NIC '+$_.Name+' '+$_.LinkSpeed }"
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaIrqMode
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaCntRxDrop
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaCntTxDrop
echo S258 W11 DONE
exit /b 0
