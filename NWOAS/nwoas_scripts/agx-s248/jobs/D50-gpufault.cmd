@echo off
echo S248 D50 GPUFAULT
powershell -NoProfile -Command "$k=Get-Item 'HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters'; foreach($n in 'Wom1GpuFault','Wom1GpuFaultMapping'){ $v=$k.GetValue($n); if($v){ 'VAL '+$n+' '+(($v|ForEach-Object{$_.ToString('x2')}) -join '') } else { 'VAL '+$n+' none' } }"
exit /b 0
