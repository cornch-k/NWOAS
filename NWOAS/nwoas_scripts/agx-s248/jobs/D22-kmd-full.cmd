@echo off
echo S248 D22 KMD FULL
powershell -NoProfile -Command "$k=Get-Item 'HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters'; foreach($n in 'Wom1DdiStatus','Wom1DisplayRing1131','Wom1G3BrokerFailures','Wom1SoftFaultState','Wom1G2PresentPath','Wom1SubmitPath1135','Wom1DisplayModesReceipt','Wom1PostDisplayRoute'){ $v=$k.GetValue($n); 'VAL '+$n+' '+$v.Length; $h=($v|ForEach-Object{$_.ToString('x2')}) -join ''; for($i=0;$i -lt $h.Length;$i+=192){ $h.Substring($i,[Math]::Min(192,$h.Length-$i)) } }"
exit /b 0
