@echo off
echo GPUCOV K18 CINEBENCH UI with its D3D11 devices on WARP (d3d11.dll proxy next to Cinebench.exe, removed afterwards); look at the dialog during the run
set D=C:\NWOAS\gpucov
set A=C:\NWOAS\cb26\app
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
icacls C:\NWOAS\cb26 /grant *S-1-5-32-545:(OI)(CI)M /T >nul
del /q %D%\warp-proxy.log 2>nul
powershell -NoProfile -Command "$a='C:\NWOAS\cb26\app'; $p='C:\NWOAS\gpucov\d3d11-warp-proxy-arm64.dll'; if(Test-Path $a\d3d11.dll){ if((Get-FileHash $a\d3d11.dll).Hash -ne (Get-FileHash $p).Hash){ 'K18 ABORT: Cinebench has its own d3d11.dll'; exit 1 } }; Copy-Item $p $a\d3d11.dll -Force; Copy-Item C:\Windows\System32\d3d11.dll $a\d3d11_sys.dll -Force; 'K18 PROXY IN PLACE'"
if errorlevel 1 exit /b 1
> %D%\k18.cmd echo @echo off
>> %D%\k18.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\cb26ui2.ps1 -Wait 75 -Warp
del /q %D%\cb26-done.txt %D%\cb26-stat.txt %D%\cb26-umd.log %D%\cb26-wintree.txt 2>nul
powershell -NoProfile -Command "$t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'WAIT explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K18 -Cmd %D%\k18.cmd -Done %D%\cb26-done.txt -Timeout 200
if not exist %D%\cb26-done.txt (echo K18 RETRY & powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K18 -Cmd %D%\k18.cmd -Done %D%\cb26-done.txt -Timeout 200)
powershell -NoProfile -Command "$a='C:\NWOAS\cb26\app'; Get-Process Cinebench* -ErrorAction SilentlyContinue | Stop-Process -Force; Start-Sleep 2; if((Get-FileHash $a\d3d11.dll).Hash -eq (Get-FileHash C:\NWOAS\gpucov\d3d11-warp-proxy-arm64.dll).Hash){ Remove-Item $a\d3d11.dll -Force }; Remove-Item $a\d3d11_sys.dll -Force -ErrorAction SilentlyContinue; 'K18 PROXY REMOVED d3d11='+(Test-Path $a\d3d11.dll)+' d3d11_sys='+(Test-Path $a\d3d11_sys.dll)"
powershell -NoProfile -Command "$d='C:\NWOAS\gpucov'; foreach($f in 'cb26-stat.txt','warp-proxy.log'){ if(Test-Path $d\$f){ Get-Content $d\$f | Select-Object -First 25 | ForEach-Object { 'K18 '+$_ } } }; if(Test-Path $d\cb26-ui.txt){ Get-Content $d\cb26-ui.txt | Select-String -Pattern 'CRITICAL|rawport|Framework|rror' | Select-Object -First 8 | ForEach-Object { 'K18 ui '+$_.Line } }; if(Test-Path $d\cb26-wintree.txt){ $l=Get-Content $d\cb26-wintree.txt; 'K18 WINTREE lines='+$l.Count; $l | Where-Object { $_ -match '^WT t50 |^WINSTAT t50_' } | Select-Object -First 90 | ForEach-Object { 'K18 '+$_ } } else { 'K18 NO WINTREE' }"
echo GPUCOV K18 DONE
exit /b 0
