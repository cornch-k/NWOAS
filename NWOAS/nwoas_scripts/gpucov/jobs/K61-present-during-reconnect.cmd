@echo off
echo GPUCOV K61 PRESENTS DURING A MONITOR RECONNECT (HV g262U test, no driver change): Edge shows the raster test page and scrolls and zooms it for 150 s, so Windows presents a new frame all the time. During those 150 s the user unplugs the monitor cable, waits 5 s and plugs it back, 5 times. PASS = the K61 ER line has fatals=0 lost=0 gpu_pid_count=1 and frames above 1000, Windows did not restart, and the HV log shows NWOAS-DCP-G262U "latched by modeset" lines without "latch MISMATCH"
set D=C:\NWOAS\gpucov
if not exist %D%\edgeraster1.ps1 (echo K61 edgeraster1.ps1 missing & echo GPUCOV K61 DONE & exit /b 0)
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
powershell -NoProfile -Command "Get-CimInstance Win32_VideoController | ForEach-Object { 'K61 VC '+$_.Name+' | '+$_.DriverVersion+' | '+$_.Status }; 'K61 start '+(Get-Date).ToString('HH:mm:ss')+' boot='+(Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToString('HH:mm:ss')"
> %D%\k-er5.cmd echo @echo off
>> %D%\k-er5.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edgeraster1.ps1 -Tag R -Out %D%\er5-R -Done %D%\er5-R-done.txt -Prof %D%\edge-prof14 -Port 9337 -Secs 150
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K61 -Cmd %D%\k-er5.cmd -Done %D%\er5-R-done.txt -Timeout 330
powershell -NoProfile -Command "Get-Content C:\NWOAS\gpucov\er5-R.txt -ErrorAction SilentlyContinue | ForEach-Object { 'K61 '+$_ }; Get-Content C:\NWOAS\gpucov\er5-R.log -ErrorAction SilentlyContinue | Select-Object -Last 12 | ForEach-Object { 'K61 LOG '+$_ }; 'K61 end '+(Get-Date).ToString('HH:mm:ss')+' boot='+(Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToString('HH:mm:ss'); Get-WinEvent -FilterHashtable @{LogName='System'; StartTime=(Get-Date).AddMinutes(-8)} -ErrorAction SilentlyContinue | Where-Object { $_.Id -in 4101,1001,41 -or $_.ProviderName -match 'Display' } | Select-Object -First 8 | ForEach-Object { $m=($_.Message -replace '\s+',' '); 'K61 EV '+$_.TimeCreated.ToString('HH:mm:ss')+' id='+$_.Id+' '+$m.Substring(0,[Math]::Min(160,$m.Length)) }; 'K61 EV end'"
echo GPUCOV K61 DONE
exit /b 0
