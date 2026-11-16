@echo off
echo S248 D42 GPURESET
powershell -NoProfile -Command "$k=Get-Item 'HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters'; foreach($n in 'Wom1GpuReset','Wom1G4PreemptOverlap'){ $v=$k.GetValue($n); 'VAL '+$n+' '+(($v|ForEach-Object{$_.ToString('x2')}) -join '') }; Get-ChildItem C:\Windows\LiveKernelReports\WATCHDOG -ErrorAction SilentlyContinue | ForEach-Object { 'LKR '+$_.Name+' '+$_.Length+' '+$_.LastWriteTime.ToString('MM-dd HH:mm:ss') }"
exit /b 0
