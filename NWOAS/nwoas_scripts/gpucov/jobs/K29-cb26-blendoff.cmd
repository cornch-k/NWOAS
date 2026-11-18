@echo off
echo GPUCOV K29 CINEBENCH UI with APPLE_AGX_D3D11_BLEND_OFF=1 (needs 2763; ask the user whether the dialog buttons and the main window panels appear)
set D=C:\NWOAS\gpucov
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
> %D%\k29.cmd echo @echo off
>> %D%\k29.cmd echo set APPLE_AGX_D3D11_BLEND_OFF=1
>> %D%\k29.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\cb26ui2.ps1 -Wait 75
del /q %D%\cb26-done.txt %D%\cb26-stat.txt %D%\cb26-umd.log %D%\cb26-wintree.txt 2>nul
powershell -NoProfile -Command "$t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'WAIT explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K29 -Cmd %D%\k29.cmd -Done %D%\cb26-done.txt -Timeout 200
if not exist %D%\cb26-done.txt (echo K29 RETRY & powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K29 -Cmd %D%\k29.cmd -Done %D%\cb26-done.txt -Timeout 200)
powershell -NoProfile -Command "$d='C:\NWOAS\gpucov'; if(Test-Path $d\cb26-stat.txt){ Get-Content $d\cb26-stat.txt | ForEach-Object { 'K29 '+$_ } }; if(Test-Path $d\cb26-umd.log){ Get-Content $d\cb26-umd.log | Where-Object { $_ -match '^measure-d3d11-knobs' } | Select-Object -First 3 | ForEach-Object { 'K29 '+$_ } }; if(Test-Path $d\cb26-wintree.txt){ Get-Content $d\cb26-wintree.txt | Where-Object { $_ -match '^WINSTAT t50_' } | Select-Object -First 12 | ForEach-Object { 'K29 '+$_ } }"
echo GPUCOV K29 DONE
exit /b 0
