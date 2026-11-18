@echo off
echo GPUCOV K19 CINEBENCH UI on our driver with the window tree and per-window pixel stats
set D=C:\NWOAS\gpucov
set A=C:\NWOAS\cb26\app
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
icacls C:\NWOAS\cb26 /grant *S-1-5-32-545:(OI)(CI)M /T >nul
rem driver mode
> %D%\k19.cmd echo @echo off
>> %D%\k19.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\cb26ui2.ps1 -Wait 75 
del /q %D%\cb26-done.txt %D%\cb26-stat.txt %D%\cb26-umd.log %D%\cb26-wintree.txt 2>nul
powershell -NoProfile -Command "$t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'WAIT explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K19 -Cmd %D%\k19.cmd -Done %D%\cb26-done.txt -Timeout 200
if not exist %D%\cb26-done.txt (echo K19 RETRY & powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K19 -Cmd %D%\k19.cmd -Done %D%\cb26-done.txt -Timeout 200)
rem nothing to undo
powershell -NoProfile -Command "$d='C:\NWOAS\gpucov'; foreach($f in 'cb26-stat.txt','warp-proxy.log'){ if(Test-Path $d\$f){ Get-Content $d\$f | Select-Object -First 25 | ForEach-Object { 'K19 '+$_ } } }; if(Test-Path $d\cb26-ui.txt){ Get-Content $d\cb26-ui.txt | Select-String -Pattern 'CRITICAL|rawport|Framework|rror' | Select-Object -First 8 | ForEach-Object { 'K19 ui '+$_.Line } }; if(Test-Path $d\cb26-wintree.txt){ $l=Get-Content $d\cb26-wintree.txt; 'K19 WINTREE lines='+$l.Count; $l | Where-Object { $_ -match '^WT t50 |^WINSTAT t50_' } | Select-Object -First 90 | ForEach-Object { 'K19 '+$_ } } else { 'K19 NO WINTREE' }"
echo GPUCOV K19 DONE
exit /b 0
