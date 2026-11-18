@echo off
echo GPUCOV K38s EDGE default, GPU sandbox ON, UMD trace by environment into a Low-integrity folder writable by Everyone/RESTRICTED/ALL APPLICATION PACKAGES (hypothesis: the sandboxed GPU process cannot write medium-integrity C:\NWOAS). PASS = ENVTRACE lines>0 and a GPU-process pid in pids=
set D=C:\NWOAS\gpucov
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
del /q %D%\edge-umd.log 2>nul
if exist C:\NWOAS\umdtrace-low rmdir /s /q C:\NWOAS\umdtrace-low
mkdir C:\NWOAS\umdtrace-low
icacls C:\NWOAS\umdtrace-low /grant *S-1-1-0:(OI)(CI)M *S-1-5-12:(OI)(CI)M *S-1-15-2-1:(OI)(CI)M >nul
icacls C:\NWOAS\umdtrace-low /setintegritylevel (OI)(CI)low
icacls C:\NWOAS\umdtrace-low
> %D%\k-edge.cmd echo @echo off
>> %D%\k-edge.cmd echo set APPLE_AGX_UMD_TRACE_FILE=C:\NWOAS\umdtrace-low\edge-umd.log
>> %D%\k-edge.cmd echo set APPLE_AGX_UMD_REFUSALS_ONLY=1
>> %D%\k-edge.cmd echo set APPLE_AGX_UMD_TRACE_SYNC=1
>> %D%\k-edge.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edge4.ps1 -Watch 45 -Extra:""
del /q %D%\edge2-done.txt 2>nul
powershell -NoProfile -Command "$t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'WAIT explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds; Start-Sleep 4"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K38s -Cmd %D%\k-edge.cmd -Done %D%\edge2-done.txt -Timeout 240
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edgelog3.ps1 -Tag K38s -EnvLog C:\NWOAS\umdtrace-low\edge-umd.log
echo GPUCOV K38s DONE
exit /b 0
