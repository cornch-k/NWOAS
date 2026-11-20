@echo off
echo GPUCOV K52e EDGE GPU RASTER A/B fifth run, daily launcher with the pool (S269), own file names so the earlier runs stay (no driver change): run A = Edge default (CPU raster), run B = --enable-gpu-rasterization (that process only). Same static page, 4 captures each via DevTools, 60 s scroll+zoom stress, capture again. PASS = the K52e VERDICT line ends with PASS
set D=C:\NWOAS\gpucov
if not exist %D%\edgeraster1.ps1 (echo K52e edgeraster1.ps1 missing - deliver gpucov19 first & echo GPUCOV K52e DONE & exit /b 0)
if not exist %D%\raster1.html (echo K52e raster1.html missing - deliver gpucov19 first & echo GPUCOV K52e DONE & exit /b 0)
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
powershell -NoProfile -Command "Get-CimInstance Win32_VideoController | ForEach-Object { 'K52e VC '+$_.Name+' | '+$_.DriverVersion+' | '+$_.Status }; $t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'K52e explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds"
> %D%\k-er6a.cmd echo @echo off
>> %D%\k-er6a.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edgeraster1.ps1 -Tag A -Out %D%\er6-A -Done %D%\er6-A-done.txt -Prof %D%\edge-prof15 -Port 9335
> %D%\k-er6b.cmd echo @echo off
>> %D%\k-er6b.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edgeraster1.ps1 -Tag B -Out %D%\er6-B -Done %D%\er6-B-done.txt -Prof %D%\edge-prof16 -Port 9336 -Extra "--enable-gpu-rasterization"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K52i -Cmd %D%\k-er6a.cmd -Done %D%\er6-A-done.txt -Timeout 300
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K52j -Cmd %D%\k-er6b.cmd -Done %D%\er6-B-done.txt -Timeout 300
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edgeraster1.ps1 -Mode cmp -A %D%\er6-A -B %D%\er6-B -Name K52e
powershell -NoProfile -Command "Get-WinEvent -FilterHashtable @{LogName='Application'; StartTime=(Get-Date).AddMinutes(-12)} -ErrorAction SilentlyContinue | Where-Object { $_.Id -in 1000,1001,1002 -and $_.Message -match 'msedge|dwm' } | Select-Object -First 6 | ForEach-Object { $m=(($_.Message -split [char]10 | Select-Object -First 3) -join ' / '); 'K52e APPEVT '+$_.Id+' '+$m.Substring(0,[Math]::Min(260,$m.Length)) }; 'K52e APPEVT end'"
echo GPUCOV K52e DONE
exit /b 0
