@echo off
rem Read-only: why did Windows reset at ~18:02 on 2026-11-20? Bugcheck / unexpected shutdown / WER / dumps.
echo E1 BEGIN
powershell -NoProfile -Command "$s=(Get-Date).AddHours(-2); Get-WinEvent -FilterHashtable @{LogName='System';StartTime=$s} -ErrorAction SilentlyContinue | Where-Object { $_.Id -in 41,1001,1074,1076,6005,6006,6008,109,12,13,20,161,4101 -or $_.ProviderName -match 'BugCheck|WER|Kernel-Power|Display|dxgkrnl' } | Sort-Object TimeCreated | ForEach-Object { 'E1 SYS ' + $_.TimeCreated.ToString('HH:mm:ss') + ' id=' + $_.Id + ' ' + $_.ProviderName + ' | ' + (($_.Message -replace '\s+',' ').Substring(0,[Math]::Min(300,($_.Message -replace '\s+',' ').Length))) }"
powershell -NoProfile -Command "$s=(Get-Date).AddHours(-2); Get-WinEvent -FilterHashtable @{LogName='Application';StartTime=$s} -ErrorAction SilentlyContinue | Where-Object { $_.Id -in 1000,1001,1002 } | Sort-Object TimeCreated | Select-Object -First 15 | ForEach-Object { 'E1 APP ' + $_.TimeCreated.ToString('HH:mm:ss') + ' id=' + $_.Id + ' | ' + (($_.Message -replace '\s+',' ').Substring(0,[Math]::Min(250,($_.Message -replace '\s+',' ').Length))) }"
powershell -NoProfile -Command "Get-ChildItem C:\Windows\Minidump -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 5 | ForEach-Object { 'E1 MINIDUMP ' + $_.Name + ' ' + $_.Length + ' ' + $_.LastWriteTime.ToString('MM-dd HH:mm:ss') }; if (Test-Path C:\Windows\MEMORY.DMP) { $m=Get-Item C:\Windows\MEMORY.DMP; 'E1 MEMORYDMP ' + $m.Length + ' ' + $m.LastWriteTime.ToString('MM-dd HH:mm:ss') } else { 'E1 MEMORYDMP none' }"
powershell -NoProfile -Command "(Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToString('MM-dd HH:mm:ss') | ForEach-Object { 'E1 LASTBOOT ' + $_ }"
echo E1 DONE
exit /b 0
