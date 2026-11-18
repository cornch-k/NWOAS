@echo off
echo GPUCOV K23d EDGE --use-angle=d3d11on12 --disable-features=Vulkan: GPU modules, log before the FATAL
set D=C:\NWOAS\gpucov
set T=C:\NWOAS\umdtrace
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
> %D%\k-edge.cmd echo @echo off
>> %D%\k-edge.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edge4.ps1 -Watch 45 -Extra:"--use-angle=d3d11on12 --disable-features=Vulkan"
del /q %D%\edge2-done.txt 2>nul
powershell -NoProfile -Command "$t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'WAIT explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds; Start-Sleep 4"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K23d -Cmd %D%\k-edge.cmd -Done %D%\edge2-done.txt -Timeout 240
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edgelog.ps1 -Tag K23d
echo GPUCOV K23d DONE
exit /b 0
