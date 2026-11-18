@echo off
echo GPUCOV K38dg EDGE --use-angle=d3d11on12 + --disable-gpu-sandbox + UMD trace (P36 D3D12 standard pattern). PASS = d3d12_alloc_fail=0 oom_lines=0 gpu_log_pids=1
set D=C:\NWOAS\gpucov
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
del /q %D%\edge-umd.log 2>nul
> %D%\k-edge.cmd echo @echo off
>> %D%\k-edge.cmd echo set APPLE_AGX_UMD_TRACE_FILE=%D%\edge-umd.log
>> %D%\k-edge.cmd echo set APPLE_AGX_UMD_REFUSALS_ONLY=1
>> %D%\k-edge.cmd echo set APPLE_AGX_UMD_TRACE_SYNC=1
>> %D%\k-edge.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edge4.ps1 -Watch 45 -Extra:"--use-angle=d3d11on12 --disable-gpu-sandbox"
del /q %D%\edge2-done.txt 2>nul
powershell -NoProfile -Command "$t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'WAIT explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds; Start-Sleep 4"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K38dg -Cmd %D%\k-edge.cmd -Done %D%\edge2-done.txt -Timeout 240
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edgelog3.ps1 -Tag K38dg
echo GPUCOV K38dg DONE
exit /b 0
