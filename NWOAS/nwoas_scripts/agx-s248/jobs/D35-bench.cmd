@echo off
echo S248 D35 BENCH
powershell -NoProfile -Command "$k=Get-Item 'HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters'; $v=$k.GetValue('NwSoftBench'); if($v){ 'NWBENCH '+(($v|ForEach-Object{$_.ToString('x2')}) -join '') } else { 'NWBENCH none' }"
exit /b 0
