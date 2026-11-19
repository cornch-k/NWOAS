@echo off
rem S268R R2 (read only): WLAN-AutoConfig event counts since THIS boot (not the last 3 h, which can include
rem earlier boots), NIC state and connectivity profile.  8003 = disconnected, 8001 = connected, 8002 = connect failed,
rem 11004/11005 = security stop/success, 4003 = autoconfig stopped, 12013 = 802.1X.  No SSID/BSSID/MAC printed.
echo S268R R2 WLAN SINCE BOOT
powershell -NoProfile -NonInteractive -Command "$b=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime; $now=Get-Date; 'R2 NOW '+$now.ToString('s')+' BOOT '+$b.ToString('s')+' UP_MS '+[int64]($now-$b).TotalMilliseconds; $L='Microsoft-Windows-WLAN-AutoConfig/Operational'; foreach($i in 4003,8001,8002,8003,11004,11005,11010,12013){ 'R2 COUNT '+$i+' '+@(Get-WinEvent -FilterHashtable @{LogName=$L;Id=$i;StartTime=$b} -ErrorAction SilentlyContinue).Count }; foreach($e in @(Get-WinEvent -FilterHashtable @{LogName=$L;Id=8003;StartTime=$b} -MaxEvents 20 -ErrorAction SilentlyContinue)){ 'R2 DISC t_ms='+[int64]($e.TimeCreated-$b).TotalMilliseconds+' '+$e.TimeCreated.ToString('HH:mm:ss') }; foreach($n in @(Get-NetAdapter -ErrorAction SilentlyContinue | Where-Object {$_.InterfaceDescription -like '*BCM4378*' -and $_.Status -ne 'Not Present'})){ 'R2 NIC status='+$n.Status+' speed='+$n.LinkSpeed; foreach($p in @(Get-NetConnectionProfile -InterfaceIndex $n.ifIndex -ErrorAction SilentlyContinue)){ if($p){ 'R2 PROF v4='+$p.IPv4Connectivity+' v6='+$p.IPv6Connectivity } } }"
echo S268R R2 DONE
exit /b 0
