@echo off
echo GPUCOV K38g EDGE default switches, GPU sandbox on, no trace (2772 P36 standard multisample pattern). PASS = fatals=0 gpu_log_pids=1 max_recent_crash=0 oom_lines=0
set D=C:\NWOAS\gpucov
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
del /q %D%\edge-umd.log 2>nul
> %D%\k-edge.cmd echo @echo off
>> %D%\k-edge.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edge4.ps1 -Watch 45 -Extra:""
del /q %D%\edge2-done.txt 2>nul
powershell -NoProfile -Command "$t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'WAIT explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds; Start-Sleep 4"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K38g -Cmd %D%\k-edge.cmd -Done %D%\edge2-done.txt -Timeout 240
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edgelog3.ps1 -Tag K38g
echo GPUCOV K38g DONE
exit /b 0
