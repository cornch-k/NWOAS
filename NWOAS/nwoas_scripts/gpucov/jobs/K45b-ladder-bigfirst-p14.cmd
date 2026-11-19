@echo off
echo GPUCOV K45b MEMLADDER BIG BUFFER FIRST (probe14 order=bigfirst on 2775): same sizes, prefix and UMD trace env as K43c, but the big DEFAULT buffer is created before the 64 MiB UPLOAD/READBACK staging buffers; then probe lines, failing UMD trace lines and a verdict; deletes Wom1PoolTimeline (latest) first and keeps Wom1PoolTimelineFirst from K45l; prints the probe pid
set D=C:\NWOAS\gpucov
set P=%D%\nwgpuprobe14-arm64.exe
powershell -NoProfile -Command "Get-CimInstance Win32_VideoController | ForEach-Object { 'K45b VC '+$_.Name+' | '+$_.DriverVersion }"
if not exist %P% (echo K45b probe14 missing & echo GPUCOV K45b DONE & exit /b 0)
reg delete "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v Wom1PoolTimeline /f >nul 2>&1
del /q %D%\k45b.txt %D%\k45b-umd.log 2>nul
setlocal
set APPLE_AGX_UMD_REFUSALS_ONLY=1
set APPLE_AGX_UMD_TRACE_SYNC=1
set APPLE_AGX_UMD_TRACE_FILE=%D%\k45b-umd.log
%P% pool dxgi memladder2 order=bigfirst ladder=1792,2048,2304,2560,3072 > %D%\k45b.txt 2>&1
endlocal
powershell -NoProfile -Command "Start-Sleep -Seconds 3; $l=@(Get-Content C:\NWOAS\gpucov\k45b.txt -ErrorAction SilentlyContinue); 'K45b lines='+$l.Count; $l | Where-Object { $_ -match 'NWGP (begin|memladder2 (pre|d3d12|summary|staging|rcpt (when|pool_demand))|d3d12 end)' } | Select-Object -First 60 | ForEach-Object { 'K45b '+$_ }; $r=@(); foreach($s in 1792,2048,2304,2560,3072){ $x=@($l -match ('memladder2 d3d12 size_mib='+$s+' .*result=(PASS|FAIL)')); $v='NONE'; if($x.Count -and ($x[0] -match 'result=(PASS|FAIL)')){ $v=$Matches[1] }; $r+=($s.ToString()+':'+$v) }; 'K45b VERDICT bigfirst '+($r -join ',')"
powershell -NoProfile -Command "$t=@(Get-Content C:\NWOAS\gpucov\k45b-umd.log -ErrorAction SilentlyContinue); 'K45b UMDTRACE lines='+$t.Count; $t | Where-Object { $_ -match '^(reject-native-allocation|measure-cpu-pool-rehome|runtime-set-error)' } | Select-Object -First 16 | ForEach-Object { 'K45b UTFAIL '+$_.Substring(0,[math]::Min(240,$_.Length)) }"
echo GPUCOV K45b DONE
exit /b 0
