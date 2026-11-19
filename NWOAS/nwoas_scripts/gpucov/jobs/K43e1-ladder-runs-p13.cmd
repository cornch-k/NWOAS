@echo off
echo GPUCOV K43e1 MEMLADDER ORDER RUNS (probe13 on 2774, replaces K43d which is over the 4032-byte mailbox): (a) 2304 alone (b) 2048 then 2304 in one process = K43c order (c) each size in its own process, 20 s apart (d) 3072 then 2304 in one process; same prefix and UMD trace env as K43c; outputs C:\NWOAS\gpucov\k43e-*.txt/log, read by K43e2-4
set D=C:\NWOAS\gpucov
set P=%D%\nwgpuprobe13-arm64.exe
powershell -NoProfile -Command "Get-CimInstance Win32_VideoController | ForEach-Object { 'K43e1 VC '+$_.Name+' | '+$_.DriverVersion }"
if not exist %P% (echo K43e1 probe13 missing & echo GPUCOV K43e1 DONE & exit /b 0)
del /q %D%\k43e-a.txt %D%\k43e-b.txt %D%\k43e-c.txt %D%\k43e-d.txt %D%\k43e-a.log %D%\k43e-b.log %D%\k43e-c.log %D%\k43e-d.log 2>nul
setlocal
set APPLE_AGX_UMD_REFUSALS_ONLY=1
set APPLE_AGX_UMD_TRACE_SYNC=1
set APPLE_AGX_UMD_TRACE_FILE=%D%\k43e-a.log
echo K43e1 a start %TIME%
%P% pool dxgi memladder2 ladder=2304 > %D%\k43e-a.txt 2>&1
powershell -NoProfile -Command "Start-Sleep -Seconds 20"
set APPLE_AGX_UMD_TRACE_FILE=%D%\k43e-b.log
echo K43e1 b start %TIME%
%P% pool dxgi memladder2 ladder=2048,2304 > %D%\k43e-b.txt 2>&1
powershell -NoProfile -Command "Start-Sleep -Seconds 20"
set APPLE_AGX_UMD_TRACE_FILE=%D%\k43e-c.log
for %%s in (1792 2048 2304 2560 3072) do (
  echo K43e1 c%%s start
  %P% pool dxgi memladder2 ladder=%%s >> %D%\k43e-c.txt 2>&1
  powershell -NoProfile -Command "Start-Sleep -Seconds 20"
)
set APPLE_AGX_UMD_TRACE_FILE=%D%\k43e-d.log
echo K43e1 d start %TIME%
%P% pool dxgi memladder2 ladder=3072,2304 > %D%\k43e-d.txt 2>&1
endlocal
powershell -NoProfile -Command "foreach($x in 'a','b','c','d'){ $f='C:\NWOAS\gpucov\k43e-'+$x+'.txt'; $l=@(Get-Content $f -ErrorAction SilentlyContinue); 'K43e1 '+$x+' lines='+$l.Count; $l | Where-Object { $_ -match 'memladder2 (pre|d3d12) ' } | ForEach-Object { 'K43e1 '+$x+' '+$_ } }"
echo GPUCOV K43e1 DONE
exit /b 0
