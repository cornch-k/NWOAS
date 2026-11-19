@echo off
echo N7 WIFI IP STATE (no addresses printed)
powershell -NoProfile -Command "$a=Get-NetAdapter | Where-Object { $_.InterfaceDescription -like '*BCM4378*' -and $_.Status -ne 'Not Present' }; foreach($n in $a){ 'N7 IF idx='+$n.ifIndex+' status='+$n.Status; $ip=Get-NetIPAddress -InterfaceIndex $n.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue; foreach($i in $ip){ 'N7 IPV4 origin='+$i.PrefixOrigin+' apipa='+($i.IPAddress -like '169.254.*')+' state='+$i.AddressState }; $r=Get-NetRoute -InterfaceIndex $n.ifIndex -DestinationPrefix '0.0.0.0/0' -ErrorAction SilentlyContinue; 'N7 DEFAULT_ROUTES '+@($r).Count; $d=Get-NetIPInterface -InterfaceIndex $n.ifIndex -AddressFamily IPv4; 'N7 DHCP '+$d.Dhcp+' connstate='+$d.ConnectionState; $s=Get-NetAdapterStatistics -Name $n.Name; 'N7 STAT rxU='+$s.ReceivedUnicastPackets+' rxB='+$s.ReceivedBroadcastPackets+' rxM='+$s.ReceivedMulticastPackets+' txU='+$s.SentUnicastPackets+' txB='+$s.SentBroadcastPackets }"
wevtutil qe Microsoft-Windows-Dhcp-Client/Admin /c:6 /rd:true /f:text 2>&1 | findstr /i "Date: Event ID: Description"
echo N7 END
exit /b 0
