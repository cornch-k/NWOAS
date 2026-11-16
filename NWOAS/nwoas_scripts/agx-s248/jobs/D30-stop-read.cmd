@echo off
echo S248 D30 STOP READ
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v G3Armed /t REG_DWORD /d 1 /f
pnputil /restart-device "ACPI\APPL0002\0"
ping -n 16 127.0.0.1 >nul
powershell -NoProfile -Command "$k=Get-Item 'HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters'; $v=$k.GetValue('NwSoftStats'); if($v){ 'NWSOFT '+(($v|ForEach-Object{$_.ToString('x2')}) -join '') } else { 'NWSOFT none' }; Get-PnpDevice -InstanceId 'ACPI\APPL0002\0' | ForEach-Object { 'DEV '+$_.Status+' '+$_.Problem }"
exit /b 0
