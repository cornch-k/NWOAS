@echo off
echo GPUCOV K45l MEMLADDER ONLY (probe14 staging first = the K43c order, on 2775): deletes the old Wom1PoolTimeline values first so K45t reads only this run; same prefix, sizes and UMD trace env as K43c's first probe line; prints the probe pid (match K45t pid=)
set D=C:\NWOAS\gpucov
set P=%D%\nwgpuprobe14-arm64.exe
set K="HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters"
powershell -NoProfile -Command "Get-CimInstance Win32_VideoController | ForEach-Object { 'K45l VC '+$_.Name+' | '+$_.DriverVersion }"
if not exist %P% (echo K45l probe14 missing & echo GPUCOV K45l DONE & exit /b 0)
reg delete %K% /v Wom1PoolTimeline /f >nul 2>&1
reg delete %K% /v Wom1PoolTimelineFirst /f >nul 2>&1
del /q %D%\k45l.txt %D%\k45l-umd.log 2>nul
setlocal
set APPLE_AGX_UMD_REFUSALS_ONLY=1
set APPLE_AGX_UMD_TRACE_SYNC=1
set APPLE_AGX_UMD_TRACE_FILE=%D%\k45l-umd.log
%P% pool dxgi memladder2 ladder=1792,2048,2304,2560,3072 > %D%\k45l.txt 2>&1
endlocal
powershell -NoProfile -Command "Start-Sleep -Seconds 3; $l=@(Get-Content C:\NWOAS\gpucov\k45l.txt -ErrorAction SilentlyContinue); 'K45l lines='+$l.Count; $l | Where-Object { $_ -match 'NWGP (begin|memladder2 (pre|d3d12|summary|staging|rcpt (when|pool_demand))|d3d12 end)' } | Select-Object -First 60 | ForEach-Object { 'K45l '+$_ }; $r=@(); foreach($s in 1792,2048,2304,2560,3072){ $x=@($l -match ('memladder2 d3d12 size_mib='+$s+' .*result=(PASS|FAIL)')); $v='NONE'; if($x.Count -and ($x[0] -match 'result=(PASS|FAIL)')){ $v=$Matches[1] }; $r+=($s.ToString()+':'+$v) }; 'K45l VERDICT stagingfirst '+($r -join ',')"
powershell -NoProfile -Command "$t=@(Get-Content C:\NWOAS\gpucov\k45l-umd.log -ErrorAction SilentlyContinue); 'K45l UMDTRACE lines='+$t.Count; $t | Where-Object { $_ -match '^(reject-native-allocation|measure-cpu-pool-rehome|runtime-set-error)' } | Select-Object -First 16 | ForEach-Object { 'K45l UTFAIL '+$_.Substring(0,[math]::Min(240,$_.Length)) }"
echo GPUCOV K45l DONE
exit /b 0
