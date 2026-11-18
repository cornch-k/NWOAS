@echo off
echo NWOAS N4 SMARTSCREEN DEBUG LOG
powershell -NoProfile -Command "Get-WinEvent -LogName 'Microsoft-Windows-SmartScreen/Debug' -MaxEvents 40 -ErrorAction SilentlyContinue | Sort-Object TimeCreated | ForEach-Object { $m=($_.Message -replace '\s+',' '); 'SSEVT '+$_.TimeCreated.ToString('HH:mm:ss')+' id='+$_.Id+' '+$m.Substring(0,[Math]::Min(400,$m.Length)) }; 'UTC_NOW '+(Get-Date).ToUniversalTime().ToString('u'); Get-WinEvent -FilterHashtable @{LogName='System'; ProviderName='Microsoft-Windows-Kernel-General'; Id=1; StartTime=(Get-Date).AddHours(-12)} -MaxEvents 5 -ErrorAction SilentlyContinue | ForEach-Object { 'TIMECHANGE '+$_.TimeCreated.ToString('u')+' '+(($_.Message -replace '\s+',' ').Substring(0,160)) }"
echo NWOAS N4 DONE
