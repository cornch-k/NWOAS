@echo off
echo S248 D29 READ STATS
powershell -NoProfile -Command "$k=Get-Item 'HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters'; $v=$k.GetValue('NwSoftStats'); if($v){ 'NWSOFT '+(($v|ForEach-Object{$_.ToString('x2')}) -join '') } else { 'NWSOFT none' }; Get-PnpDevice -InstanceId 'ACPI\APPL0002\0' | ForEach-Object { 'DEV '+$_.Status }"
exit /b 0
