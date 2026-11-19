@echo off
echo GPUCOV K45r MEMLADDER REPEAT x3 (probe14 staging first = K43c order, on 2775, recorder on): each run = new process, ladder 1792..3072 then rcpt (pool places/kills, paging DDI counters); stops repeating at the first FAIL so Wom1PoolTimelineFirst keeps it
set D=C:\NWOAS\gpucov
set P=%D%\nwgpuprobe14-arm64.exe
powershell -NoProfile -Command "Get-CimInstance Win32_VideoController | ForEach-Object { 'K45r VC '+$_.Name+' | '+$_.DriverVersion }"
if not exist %P% (echo K45r probe14 missing & echo GPUCOV K45r DONE & exit /b 0)
setlocal
set APPLE_AGX_UMD_REFUSALS_ONLY=1
set APPLE_AGX_UMD_TRACE_SYNC=1
for %%i in (1 2 3) do (
  del /q %D%\k45r-%%i.txt %D%\k45r-%%i.log 2>nul
  set APPLE_AGX_UMD_TRACE_FILE=%D%\k45r-%%i.log
  %P% pool dxgi memladder2 rcpt ladder=1792,2048,2304,2560,3072 > %D%\k45r-%%i.txt 2>&1
  powershell -NoProfile -Command "$l=@(Get-Content C:\NWOAS\gpucov\k45r-%%i.txt -ErrorAction SilentlyContinue); $l | Where-Object { $_ -match 'NWGP (begin|memladder2 (d3d12|summary)|rcpt rcpt (pool_demand|ddi_status slot=(39|82|88|89) ))' } | ForEach-Object { 'K45r %%i '+$_.Substring(0,[math]::Min(330,$_.Length)) }; $t=@(Get-Content C:\NWOAS\gpucov\k45r-%%i.log -ErrorAction SilentlyContinue); $t | Where-Object { $_ -match '^(reject-native-allocation|measure-cpu-pool-rehome)' } | Select-Object -First 6 | ForEach-Object { 'K45r %%i UTFAIL '+$_.Substring(0,[math]::Min(240,$_.Length)) }; if(@($l -match 'result=FAIL').Count){ exit 1 } else { exit 0 }"
  if errorlevel 1 (echo K45r %%i FAIL seen - stop repeating & goto :end)
  powershell -NoProfile -Command "Start-Sleep -Seconds 5"
)
:end
endlocal
powershell -NoProfile -Command "$r=@(); foreach($i in 1,2,3){ $l=@(Get-Content ('C:\NWOAS\gpucov\k45r-'+$i+'.txt') -ErrorAction SilentlyContinue); if(-not $l.Count){ continue }; $f=@($l -match 'memladder2 d3d12 size_mib=\d+ .*result=FAIL' | ForEach-Object { if($_ -match 'size_mib=(\d+)'){ $Matches[1] } }); $r+=('run'+$i+':'+$(if($f.Count){'FAIL@'+($f -join '/')}else{'PASS'})) }; 'K45r VERDICT '+($r -join ' ')"
echo GPUCOV K45r DONE
exit /b 0
