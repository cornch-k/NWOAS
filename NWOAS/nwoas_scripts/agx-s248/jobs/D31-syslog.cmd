@echo off
echo S248 D31 SYSLOG
powershell -NoProfile -Command "Get-WinEvent -FilterHashtable @{LogName='System';StartTime=(Get-Date).AddMinutes(-20)} -ErrorAction SilentlyContinue | Where-Object { $_.ProviderName -notmatch 'Service Control|Time-Service|DistributedCOM|WindowsUpdate|Kernel-General|Kernel-Power|Winlogon|User32' } | Select-Object -First 40 | ForEach-Object { $_.TimeCreated.ToString('HH:mm:ss')+' '+$_.ProviderName+' '+$_.Id+' '+(($_.Message -replace '\s+',' ') | ForEach-Object { $_.Substring(0,[Math]::Min(160,$_.Length)) }) }"
powershell -NoProfile -Command "$k=Get-Item 'HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters'; foreach($n in 'Wom1StartStage','Wom1StartStatus','Wom1GpuvaArmPhase','Wom1GpuvaArmStatus','Wom1PlatformStage','Wom1PlatformStatus','Wom1MemoryStopStatus','Wom1Context0RetireResult'){ $n+' = '+$k.GetValue($n) }"
exit /b 0
