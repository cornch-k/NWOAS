@echo off
echo S258 W10 NET CHECK
powershell -NoProfile -Command "Get-NetAdapter | Where-Object {$_.InterfaceDescription -like '*BCM4378*' -and $_.Status -ne 'Not Present'} | ForEach-Object { 'NIC '+$_.Name+' status='+$_.Status+' speed='+$_.LinkSpeed+' mac='+$_.MacAddress }; Get-NetIPConfiguration | Where-Object {$_.InterfaceDescription -like '*BCM4378*'} | ForEach-Object { 'IP '+($_.IPv4Address.IPAddress -join ',')+' gw='+($_.IPv4DefaultGateway.NextHop -join ',')+' dns='+($_.DNSServer.ServerAddresses -join ',') }"
ping -n 4 192.168.1.1
ping -n 4 1.1.1.1
powershell -NoProfile -Command "try { $t=Measure-Command { $r=Invoke-WebRequest -UseBasicParsing -Uri 'http://www.msftconnecttest.com/connecttest.txt' -TimeoutSec 15 }; 'HTTP '+$r.StatusCode+' '+$r.Content+' '+[int]$t.TotalMilliseconds+' ms' } catch { 'HTTP FAIL '+$_.Exception.Message }"
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaStage
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaCntRx
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaCntTx
echo S258 W10 DONE
exit /b 0
