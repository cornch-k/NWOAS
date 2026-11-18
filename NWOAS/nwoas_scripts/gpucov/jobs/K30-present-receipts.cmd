@echo off
echo GPUCOV K30 KMD PRESENT RECEIPTS after K25
powershell -NoProfile -Command "$k='HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters'; $p=Get-ItemProperty $k; foreach($n in ($p.PSObject.Properties.Name | Where-Object { $_ -like 'Wom1*Present*' })){ $v=$p.$n; if($v -is [byte[]]){ $w=@(); for($i=0;$i -lt $v.Length;$i+=4){ $w+=('{0:x8}' -f [BitConverter]::ToUInt32($v,$i)) }; 'R '+$n+' '+($w -join ' ') } else { 'R '+$n+' '+$v } }"
echo GPUCOV K30 DONE
exit /b 0
