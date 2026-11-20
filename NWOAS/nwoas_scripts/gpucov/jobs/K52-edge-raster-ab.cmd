@echo off
echo GPUCOV K52 EDGE GPU RASTER A/B (no driver change): run A = Edge default (CPU raster), run B = --enable-gpu-rasterization (that process only). Same static page, 4 captures each via DevTools, 60 s scroll+zoom stress, capture again. PASS = the K52 VERDICT line ends with PASS
set D=C:\NWOAS\gpucov
if not exist %D%\edgeraster1.ps1 (echo K52 edgeraster1.ps1 missing - deliver gpucov19 first & echo GPUCOV K52 DONE & exit /b 0)
if not exist %D%\raster1.html (echo K52 raster1.html missing - deliver gpucov19 first & echo GPUCOV K52 DONE & exit /b 0)
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
powershell -NoProfile -Command "Get-CimInstance Win32_VideoController | ForEach-Object { 'K52 VC '+$_.Name+' | '+$_.DriverVersion+' | '+$_.Status }; $t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'K52 explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds"
> %D%\k-er1.cmd echo @echo off
>> %D%\k-er1.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edgeraster1.ps1 -Tag A -Out %D%\er-A -Done %D%\er-A-done.txt -Prof %D%\edge-prof6 -Port 9335
> %D%\k-er2.cmd echo @echo off
>> %D%\k-er2.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edgeraster1.ps1 -Tag B -Out %D%\er-B -Done %D%\er-B-done.txt -Prof %D%\edge-prof7 -Port 9336 -Extra "--enable-gpu-rasterization"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K52a -Cmd %D%\k-er1.cmd -Done %D%\er-A-done.txt -Timeout 300
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K52b -Cmd %D%\k-er2.cmd -Done %D%\er-B-done.txt -Timeout 300
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edgeraster1.ps1 -Mode cmp -A %D%\er-A -B %D%\er-B -Name K52
powershell -NoProfile -Command "Get-WinEvent -FilterHashtable @{LogName='Application'; StartTime=(Get-Date).AddMinutes(-12)} -ErrorAction SilentlyContinue | Where-Object { $_.Id -in 1000,1001,1002 -and $_.Message -match 'msedge|dwm' } | Select-Object -First 6 | ForEach-Object { $m=(($_.Message -split [char]10 | Select-Object -First 3) -join ' / '); 'K52 APPEVT '+$_.Id+' '+$m.Substring(0,[Math]::Min(260,$m.Length)) }; 'K52 APPEVT end'"
echo GPUCOV K52 DONE
exit /b 0
