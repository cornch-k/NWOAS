@echo off
echo GPUCOV K37m EDGE default ANGLE D3D11 + APPLE_AGX_D3D_MSAA_CAPS=80 (MULTISAMPLE_RENDERTARGET bit, 1x only) + UMD trace
set D=C:\NWOAS\gpucov
set T=C:\NWOAS\umdtrace
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
del /q %D%\edge-umd.log 2>nul
> %D%\k-edge.cmd echo @echo off
>> %D%\k-edge.cmd echo set APPLE_AGX_UMD_TRACE_FILE=%D%\edge-umd.log
>> %D%\k-edge.cmd echo set APPLE_AGX_UMD_REFUSALS_ONLY=1
>> %D%\k-edge.cmd echo set APPLE_AGX_UMD_TRACE_SYNC=1
>> %D%\k-edge.cmd echo set APPLE_AGX_D3D_MSAA_CAPS=80
>> %D%\k-edge.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edge4.ps1 -Watch 45 -Extra:""
del /q %D%\edge2-done.txt 2>nul
powershell -NoProfile -Command "$t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'WAIT explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds; Start-Sleep 4"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K37m -Cmd %D%\k-edge.cmd -Done %D%\edge2-done.txt -Timeout 240
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edgelog2.ps1 -Tag K37m
echo GPUCOV K37m DONE
exit /b 0
