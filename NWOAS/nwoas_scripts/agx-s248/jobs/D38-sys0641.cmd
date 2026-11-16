@echo off
echo S248 D38 SYSTEM 06:38-06:44
powershell -NoProfile -Command "$t0=(Get-Date).Date.AddHours(6).AddMinutes(38); Get-WinEvent -FilterHashtable @{LogName='System';StartTime=$t0} -ErrorAction SilentlyContinue | Sort-Object TimeCreated | Select-Object -First 25 | ForEach-Object { $_.TimeCreated.ToString('HH:mm:ss')+' '+$_.ProviderName+' '+$_.Id+' '+(($_.Message -replace '\s+',' ') | ForEach-Object { $_.Substring(0,[Math]::Min(150,$_.Length)) }) }"
powershell -NoProfile -Command "$t0=(Get-Date).Date.AddHours(6).AddMinutes(38); Get-WinEvent -FilterHashtable @{LogName='Application';StartTime=$t0} -ErrorAction SilentlyContinue | Where-Object { $_.ProviderName -match 'Desktop Window|Dwm|Winlogon|Display' } | Select-Object -First 10 | ForEach-Object { $_.TimeCreated.ToString('HH:mm:ss')+' '+$_.ProviderName+' '+$_.Id+' '+(($_.Message -replace '\s+',' ') | ForEach-Object { $_.Substring(0,[Math]::Min(150,$_.Length)) }) }"
exit /b 0
