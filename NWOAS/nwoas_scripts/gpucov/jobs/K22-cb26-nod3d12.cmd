@echo off
echo GPUCOV K22 same as K21 with APPLE_AGX_D3D12=0 for Cinebench (our D3D12 UMD refuses this process, so it must use D3D11)
set D=C:\NWOAS\gpucov
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
icacls C:\NWOAS\cb26 /grant *S-1-5-32-545:(OI)(CI)M /T >nul
del /q %D%\dxgi-proxy.log 2>nul
powershell -NoProfile -Command "$a='C:\NWOAS\cb26\app'; $p='C:\NWOAS\gpucov\dxgi-trace-proxy-arm64.dll'; if(Test-Path $a\dxgi.dll){ if((Get-FileHash $a\dxgi.dll).Hash -ne (Get-FileHash $p).Hash){ 'K22 ABORT: Cinebench has its own dxgi.dll'; exit 1 } }; Copy-Item $p $a\dxgi.dll -Force; Copy-Item C:\Windows\System32\dxgi.dll $a\dxgi_sys.dll -Force; 'K22 DXGI PROXY IN PLACE'"
if errorlevel 1 exit /b 1
> %D%\k22.cmd echo @echo off
>> %D%\k22.cmd echo set APPLE_AGX_D3D_DRAW_STATE=1
>> %D%\k22.cmd echo set APPLE_AGX_D3D12=0
>> %D%\k22.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\cb26ui2.ps1 -Wait 75
del /q %D%\cb26-done.txt %D%\cb26-stat.txt %D%\cb26-umd.log %D%\cb26-wintree.txt 2>nul
powershell -NoProfile -Command "$t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'WAIT explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K22 -Cmd %D%\k22.cmd -Done %D%\cb26-done.txt -Timeout 200
if not exist %D%\cb26-done.txt (echo K22 RETRY & powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K22 -Cmd %D%\k22.cmd -Done %D%\cb26-done.txt -Timeout 200)
powershell -NoProfile -Command "$a='C:\NWOAS\cb26\app'; Get-Process Cinebench* -ErrorAction SilentlyContinue | Stop-Process -Force; Start-Sleep 3; if((Get-FileHash $a\dxgi.dll).Hash -eq (Get-FileHash C:\NWOAS\gpucov\dxgi-trace-proxy-arm64.dll).Hash){ Remove-Item $a\dxgi.dll -Force }; Remove-Item $a\dxgi_sys.dll -Force -ErrorAction SilentlyContinue; 'K22 DXGI PROXY REMOVED dxgi='+(Test-Path $a\dxgi.dll)+' dxgi_sys='+(Test-Path $a\dxgi_sys.dll)"
powershell -NoProfile -Command "$d='C:\NWOAS\gpucov'; foreach($f in 'cb26-stat.txt','dxgi-proxy.log'){ if(Test-Path $d\$f){ Get-Content $d\$f | Select-Object -First 120 | ForEach-Object { 'K22 '+$_ } } else { 'K22 NO '+$f } }; if(Test-Path $d\cb26-wintree.txt){ Get-Content $d\cb26-wintree.txt | Where-Object { $_ -match '^WT t50 |^WINSTAT t50_' } | Select-Object -First 70 | ForEach-Object { 'K22 '+$_ } }"
powershell -NoProfile -Command "$t='C:\NWOAS\gpucov\cb26-umd.log'; if(!(Test-Path $t)){'K22 NO TRACE';exit}; $l=@(Get-Content $t); 'K22 TRACE lines='+$l.Count; $l | Where-Object { $_ -match '^(measure-d3d11-drawstate|measure-d3d12-draw|measure-present-before|measure-d3d12-owner|measure-d3d11-shader |measure-d3d12-pso ) .*pid=(\d+)' } | ForEach-Object { $p=[regex]::Match($_,'pid=(\d+)').Groups[1].Value; $n=($_ -split ' ')[0]; $v=$_ -split ' '; $extra=$(if($n -eq 'measure-d3d11-drawstate'){ ' rt_w='+[Convert]::ToInt32($v[27],16)+' fb='+[Convert]::ToInt32($v[33],16)+'x'+[Convert]::ToInt32($v[34],16) } else { '' }); 'pid='+$p+' '+$n+$extra } | Group-Object | Sort-Object Name | ForEach-Object { 'K22 PERPID '+$_.Count+' '+$_.Name }"
echo GPUCOV K22 DONE
exit /b 0
