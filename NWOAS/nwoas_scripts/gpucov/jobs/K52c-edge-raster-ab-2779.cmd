@echo off
echo GPUCOV K52c EDGE GPU RASTER A/B third run on 2779, own file names so the earlier runs stay (no driver change): run A = Edge default (CPU raster), run B = --enable-gpu-rasterization (that process only). Same static page, 4 captures each via DevTools, 60 s scroll+zoom stress, capture again. PASS = the K52c VERDICT line ends with PASS
set D=C:\NWOAS\gpucov
if not exist %D%\edgeraster1.ps1 (echo K52c edgeraster1.ps1 missing - deliver gpucov19 first & echo GPUCOV K52c DONE & exit /b 0)
if not exist %D%\raster1.html (echo K52c raster1.html missing - deliver gpucov19 first & echo GPUCOV K52c DONE & exit /b 0)
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
powershell -NoProfile -Command "Get-CimInstance Win32_VideoController | ForEach-Object { 'K52c VC '+$_.Name+' | '+$_.DriverVersion+' | '+$_.Status }; $t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'K52c explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds"
> %D%\k-er3a.cmd echo @echo off
>> %D%\k-er3a.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edgeraster1.ps1 -Tag A -Out %D%\er3-A -Done %D%\er3-A-done.txt -Prof %D%\edge-prof10 -Port 9335
> %D%\k-er3b.cmd echo @echo off
>> %D%\k-er3b.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edgeraster1.ps1 -Tag B -Out %D%\er3-B -Done %D%\er3-B-done.txt -Prof %D%\edge-prof11 -Port 9336 -Extra "--enable-gpu-rasterization"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K52e -Cmd %D%\k-er3a.cmd -Done %D%\er3-A-done.txt -Timeout 300
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K52f -Cmd %D%\k-er3b.cmd -Done %D%\er3-B-done.txt -Timeout 300
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edgeraster1.ps1 -Mode cmp -A %D%\er3-A -B %D%\er3-B -Name K52c
powershell -NoProfile -Command "Get-WinEvent -FilterHashtable @{LogName='Application'; StartTime=(Get-Date).AddMinutes(-12)} -ErrorAction SilentlyContinue | Where-Object { $_.Id -in 1000,1001,1002 -and $_.Message -match 'msedge|dwm' } | Select-Object -First 6 | ForEach-Object { $m=(($_.Message -split [char]10 | Select-Object -First 3) -join ' / '); 'K52c APPEVT '+$_.Id+' '+$m.Substring(0,[Math]::Min(260,$m.Length)) }; 'K52c APPEVT end'"
echo GPUCOV K52c DONE
exit /b 0
