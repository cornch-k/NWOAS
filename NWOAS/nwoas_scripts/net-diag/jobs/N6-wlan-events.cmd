@echo off
echo NWOAS N6 WLAN AUTOCONFIG EVENTS (last 2 h)
powershell -NoProfile -Command "Get-WinEvent -FilterHashtable @{LogName='Microsoft-Windows-WLAN-AutoConfig/Operational'; StartTime=(Get-Date).AddHours(-2)} -ErrorAction SilentlyContinue | Sort-Object TimeCreated | ForEach-Object { $m=($_.Message -replace '\s+',' '); $m=$m -replace '(SSID|Network SSID|BSSID|Peer)[^:]*: *[^ ]+','$1: x'; 'WEV '+$_.TimeCreated.ToString('HH:mm:ss')+' id='+$_.Id+' '+$m.Substring(0,[Math]::Min(330,$m.Length)) }"
powershell -NoProfile -Command "Get-WinEvent -FilterHashtable @{LogName='System'; StartTime=(Get-Date).AddHours(-2)} -ErrorAction SilentlyContinue | Where-Object { $_.ProviderName -match 'WLAN|NWOA|Netwtw|Tcpip|Dhcp|NDIS|WiFiCx|Kernel-Power' } | Sort-Object TimeCreated | ForEach-Object { $m=($_.Message -replace '\s+',' '); 'SEV '+$_.TimeCreated.ToString('HH:mm:ss')+' '+$_.ProviderName+' id='+$_.Id+' '+$m.Substring(0,[Math]::Min(250,$m.Length)) }"
echo NWOAS N6 DONE
