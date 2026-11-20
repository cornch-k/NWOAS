@echo off
echo GPUCOV K52d EDGE GPU RASTER A/B fourth run on 2780, own file names so the earlier runs stay (no driver change): run A = Edge default (CPU raster), run B = --enable-gpu-rasterization (that process only). Same static page, 4 captures each via DevTools, 60 s scroll+zoom stress, capture again. PASS = the K52d VERDICT line ends with PASS
set D=C:\NWOAS\gpucov
if not exist %D%\edgeraster1.ps1 (echo K52d edgeraster1.ps1 missing - deliver gpucov19 first & echo GPUCOV K52d DONE & exit /b 0)
if not exist %D%\raster1.html (echo K52d raster1.html missing - deliver gpucov19 first & echo GPUCOV K52d DONE & exit /b 0)
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
powershell -NoProfile -Command "Get-CimInstance Win32_VideoController | ForEach-Object { 'K52d VC '+$_.Name+' | '+$_.DriverVersion+' | '+$_.Status }; $t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'K52d explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds"
> %D%\k-er4a.cmd echo @echo off
>> %D%\k-er4a.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edgeraster1.ps1 -Tag A -Out %D%\er4-A -Done %D%\er4-A-done.txt -Prof %D%\edge-prof12 -Port 9335
> %D%\k-er4b.cmd echo @echo off
>> %D%\k-er4b.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edgeraster1.ps1 -Tag B -Out %D%\er4-B -Done %D%\er4-B-done.txt -Prof %D%\edge-prof13 -Port 9336 -Extra "--enable-gpu-rasterization"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K52g -Cmd %D%\k-er4a.cmd -Done %D%\er4-A-done.txt -Timeout 300
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K52h -Cmd %D%\k-er4b.cmd -Done %D%\er4-B-done.txt -Timeout 300
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edgeraster1.ps1 -Mode cmp -A %D%\er4-A -B %D%\er4-B -Name K52d
powershell -NoProfile -Command "Get-WinEvent -FilterHashtable @{LogName='Application'; StartTime=(Get-Date).AddMinutes(-12)} -ErrorAction SilentlyContinue | Where-Object { $_.Id -in 1000,1001,1002 -and $_.Message -match 'msedge|dwm' } | Select-Object -First 6 | ForEach-Object { $m=(($_.Message -split [char]10 | Select-Object -First 3) -join ' / '); 'K52d APPEVT '+$_.Id+' '+$m.Substring(0,[Math]::Min(260,$m.Length)) }; 'K52d APPEVT end'"
echo GPUCOV K52d DONE
exit /b 0
