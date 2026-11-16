@echo off
echo S248 D46 QUIESCE
powershell -NoProfile -Command "$k=Get-Item 'HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters'; foreach($n in 'Wom1G3CopyPagingQuiescence','Wom1G3CopyTransferFailure','Wom1G3PagingFailure','Wom1G3PagingWait','Wom1G3PagingStatus'){ $v=$k.GetValue($n); if($v -is [byte[]]){ 'VAL '+$n+' '+(($v|ForEach-Object{$_.ToString('x2')}) -join '') } else { 'VAL '+$n+' '+$v } }"
exit /b 0
