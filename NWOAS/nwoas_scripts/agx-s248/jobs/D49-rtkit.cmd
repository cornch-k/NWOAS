@echo off
echo S248 D49 RTKIT
powershell -NoProfile -Command "$k=Get-Item 'HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters'; foreach($n in $k.GetValueNames()){ if($n -match 'Rtkit|Crash|Correlation|FirmwareFail|FirmwareRecover|Recovery|Halt'){ $v=$k.GetValue($n); if($v -is [byte[]]){ ''+$n+' BIN '+$v.Length+' '+(($v|Select-Object -First 96|ForEach-Object{$_.ToString('x2')}) -join '') } else { ''+$n+' = '+$v } } }; Get-PnpDevice -InstanceId 'ACPI\APPL0002\0' | ForEach-Object { 'DEV '+$_.Status+' '+$_.Problem }"
exit /b 0
