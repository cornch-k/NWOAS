@echo off
echo S248 D25 ENABLE WITH DISABLEDDISPLAY
reg add "HKLM\SOFTWARE\Microsoft\Windows\Dwm" /v DisableDDisplay /t REG_DWORD /d 1 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v G3Armed /t REG_DWORD /d 1 /f
powershell -NoProfile -Command "(Get-Date).ToString('o') | Set-Content C:\NWOAS\d25-start.txt"
pnputil /enable-device "ACPI\APPL0002\0"
ping -n 61 127.0.0.1 >nul
powershell -NoProfile -Command "$t=[datetime](Get-Content C:\NWOAS\d25-start.txt); $e=@(Get-WinEvent -FilterHashtable @{LogName='Application';Id=1000;StartTime=$t} -ErrorAction SilentlyContinue | Where-Object { $_.Properties[0].Value -eq 'dwm.exe' }); 'DWM CRASHES SINCE START '+$e.Count; $e|Select-Object -First 3|ForEach-Object{ $_.TimeCreated.ToString('HH:mm:ss')+' '+$_.Properties[6].Value+' '+$_.Properties[7].Value }; Get-Process dwm -ErrorAction SilentlyContinue | ForEach-Object { 'DWM PID '+$_.Id+' START '+$_.StartTime.ToString('HH:mm:ss') }; Get-PnpDevice -InstanceId 'ACPI\APPL0002\0' | ForEach-Object { 'DEV '+$_.Status+' '+$_.Problem }; if(Test-Path C:\NWOAS\umd-trace.log){ $l=Get-Content C:\NWOAS\umd-trace.log; 'TRACE LINES '+$l.Count; $l | ForEach-Object { ($_ -split ' ')[0]+' '+($_ -split ' ')[1] } | Group-Object | Sort-Object Count -Descending | Select-Object -First 25 | ForEach-Object { ''+$_.Count+' '+$_.Name } }; if($e.Count -ge 2){ 'TOO MANY CRASHES: DISABLING DEVICE'; pnputil /disable-device 'ACPI\APPL0002\0' } else { 'KEEPING DEVICE ENABLED' }"
exit /b 0
