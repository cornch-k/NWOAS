@echo off
echo S248 D27 START FAIL DETAIL
powershell -NoProfile -Command "$k=Get-Item 'HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters'; foreach($n in $k.GetValueNames()){ $v=$k.GetValue($n); if($n -match 'Stage|Status|Result|Phase|Scanout|Soft|Fail|Error|Refus|Post|Arm|Display'){ if($v -is [byte[]]){ ''+$n+' BIN '+$v.Length+' '+(($v|Select-Object -First 64|ForEach-Object{$_.ToString('x2')}) -join '') } else { ''+$n+' = '+$v } } }"
powershell -NoProfile -Command "Get-WinEvent -FilterHashtable @{LogName='System';StartTime=(Get-Date).AddMinutes(-15)} -ErrorAction SilentlyContinue | Where-Object { $_.ProviderName -match 'Display|Kernel-PnP|dxgkrnl|DeviceSetup' -or $_.Message -match 'APPL0002' } | Select-Object -First 20 | ForEach-Object { $_.TimeCreated.ToString('HH:mm:ss')+' '+$_.ProviderName+' '+$_.Id+' '+(($_.Message -replace '\s+',' ').Substring(0,[Math]::Min(200,$_.Message.Length))) }"
exit /b 0
