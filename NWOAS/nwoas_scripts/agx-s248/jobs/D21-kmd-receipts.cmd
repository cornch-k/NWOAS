@echo off
echo S248 D21 KMD RECEIPTS
powershell -NoProfile -Command "foreach($p in 'HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters','HKLM:\SYSTEM\CurrentControlSet\Services\AppleAgxAdmission'){ 'KEY '+$p; $k=Get-Item $p; foreach($n in $k.GetValueNames()){ $v=$k.GetValue($n); if($v -is [byte[]]){ ''+$n+' BIN '+$v.Length+' '+(($v|Select-Object -First 96|ForEach-Object{$_.ToString('x2')}) -join '') } else { ''+$n+' = '+$v } } }"
exit /b 0
