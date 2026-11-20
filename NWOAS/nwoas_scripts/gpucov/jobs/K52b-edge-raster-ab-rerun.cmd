@echo off
echo GPUCOV K52b EDGE GPU RASTER A/B second run, own file names so the first run stays (no driver change): run A = Edge default (CPU raster), run B = --enable-gpu-rasterization (that process only). Same static page, 4 captures each via DevTools, 60 s scroll+zoom stress, capture again. PASS = the K52b VERDICT line ends with PASS
set D=C:\NWOAS\gpucov
if not exist %D%\edgeraster1.ps1 (echo K52b edgeraster1.ps1 missing - deliver gpucov19 first & echo GPUCOV K52b DONE & exit /b 0)
if not exist %D%\raster1.html (echo K52b raster1.html missing - deliver gpucov19 first & echo GPUCOV K52b DONE & exit /b 0)
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
powershell -NoProfile -Command "Get-CimInstance Win32_VideoController | ForEach-Object { 'K52b VC '+$_.Name+' | '+$_.DriverVersion+' | '+$_.Status }; $t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'K52b explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds"
> %D%\k-er2a.cmd echo @echo off
>> %D%\k-er2a.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edgeraster1.ps1 -Tag A -Out %D%\er2-A -Done %D%\er2-A-done.txt -Prof %D%\edge-prof8 -Port 9335
> %D%\k-er2b.cmd echo @echo off
>> %D%\k-er2b.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edgeraster1.ps1 -Tag B -Out %D%\er2-B -Done %D%\er2-B-done.txt -Prof %D%\edge-prof9 -Port 9336 -Extra "--enable-gpu-rasterization"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K52c -Cmd %D%\k-er2a.cmd -Done %D%\er2-A-done.txt -Timeout 300
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K52d -Cmd %D%\k-er2b.cmd -Done %D%\er2-B-done.txt -Timeout 300
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edgeraster1.ps1 -Mode cmp -A %D%\er2-A -B %D%\er2-B -Name K52b
powershell -NoProfile -Command "Get-WinEvent -FilterHashtable @{LogName='Application'; StartTime=(Get-Date).AddMinutes(-12)} -ErrorAction SilentlyContinue | Where-Object { $_.Id -in 1000,1001,1002 -and $_.Message -match 'msedge|dwm' } | Select-Object -First 6 | ForEach-Object { $m=(($_.Message -split [char]10 | Select-Object -First 3) -join ' / '); 'K52b APPEVT '+$_.Id+' '+$m.Substring(0,[Math]::Min(260,$m.Length)) }; 'K52b APPEVT end'"
echo GPUCOV K52b DONE
exit /b 0
