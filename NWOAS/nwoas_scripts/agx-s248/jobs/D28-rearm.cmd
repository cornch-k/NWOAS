@echo off
echo S248 D28 REARM + RESTART
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v G3Armed /t REG_DWORD /d 1 /f
pnputil /restart-device "ACPI\APPL0002\0"
ping -n 46 127.0.0.1 >nul
powershell -NoProfile -Command "Get-PnpDevice -InstanceId 'ACPI\APPL0002\0' | ForEach-Object { 'DEV '+$_.Status+' '+$_.Problem }; $k=Get-Item 'HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters'; foreach($n in 'Wom1StartStage','Wom1StartStatus','Wom1GpuvaArmPhase','Wom1GpuvaArmStatus'){ $n+' = '+$k.GetValue($n) }; $v=$k.GetValue('NwSoftStats'); if($v){ 'NWSOFT '+(($v|ForEach-Object{$_.ToString('x2')}) -join '') } else { 'NWSOFT none' }; $e=@(Get-WinEvent -FilterHashtable @{LogName='Application';Id=1000;StartTime=(Get-Date).AddMinutes(-2)} -ErrorAction SilentlyContinue | Where-Object { $_.Properties[0].Value -eq 'dwm.exe' }); 'DWM CRASHES '+$e.Count"
exit /b 0
