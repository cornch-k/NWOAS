@echo off
echo GPUCOV K32 KMD DDI RINGS (2765+): Present, standard allocation, open/create allocation rings, G1b failures, present receipts; all Wom1 value names
powershell -NoProfile -Command "$k='HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters'; $p=Get-ItemProperty $k; $names=@($p.PSObject.Properties.Name | Where-Object { $_ -like 'Wom1*' -or $_ -like 'Nw*' }); 'K32 names='+($names -join ','); foreach($n in ($names | Where-Object { $_ -match 'Ring$|G1bFailure|PresentReceipt|PresentDdi$|PresentOpenFailure' })){ $v=$p.$n; if($v -is [byte[]]){ $w=@(); for($i=0;$i+3 -lt $v.Length;$i+=4){ $w+=('{0:x}' -f [BitConverter]::ToUInt32($v,$i)) }; if($n -match 'Ring$'){ 'K32 '+$n+' hdr '+($w[0..7] -join ' '); for($s=0;$s -lt 16;$s++){ $e=$w[(8+$s*24)..(8+$s*24+23)]; if(($e | Where-Object { $_ -ne '0' }).Count){ 'K32 '+$n+' ['+$s+'] '+($e -join ' ') } } } else { 'K32 '+$n+' '+($w -join ' ') } } else { 'K32 '+$n+' '+$v } }"
echo GPUCOV K32 DONE
exit /b 0
