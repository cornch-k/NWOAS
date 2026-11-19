@echo off
echo GPUCOV K46a A/B LEGS on 2776 (probe14 ladder 1792..3072 staging first, one new process per leg, recorder on): off R off P off R off P. off = 2775 behaviour; R = APPLE_AGX_CPU_POOL_REHOME_ROUNDS=16 + _SLEEP_MS=250; P = APPLE_AGX_CPU_POOL_PRIORITY=2 (MAXIMUM). A switch counts only when its legs pass while an off leg of the same boot fails
set D=C:\NWOAS\gpucov
set P=%D%\nwgpuprobe14-arm64.exe
powershell -NoProfile -Command "Get-CimInstance Win32_VideoController | ForEach-Object { 'K46a VC '+$_.Name+' | '+$_.DriverVersion }; 'K46a TopDown='+(Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters' -ErrorAction SilentlyContinue).Wom1PoolCpuVisibleTopDown"
if not exist %P% (echo K46a probe14 missing & echo GPUCOV K46a DONE & exit /b 0)
call :leg 1 off
call :leg 2 R
call :leg 3 off
call :leg 4 P
call :leg 5 off
call :leg 6 R
call :leg 7 off
call :leg 8 P
echo GPUCOV K46a DONE
exit /b 0
:leg
setlocal
del /q %D%\k46a-%1.txt %D%\k46a-%1.log 2>nul
set APPLE_AGX_UMD_REFUSALS_ONLY=1
set APPLE_AGX_UMD_TRACE_SYNC=1
set APPLE_AGX_UMD_TRACE_FILE=%D%\k46a-%1.log
if "%2"=="R" set APPLE_AGX_CPU_POOL_REHOME_ROUNDS=16
if "%2"=="R" set APPLE_AGX_CPU_POOL_REHOME_SLEEP_MS=250
if "%2"=="P" set APPLE_AGX_CPU_POOL_PRIORITY=2
%P% pool dxgi memladder2 ladder=1792,2048,2304,2560,3072 > %D%\k46a-%1.txt 2>&1
powershell -NoProfile -Command "$l=@(Get-Content C:\NWOAS\gpucov\k46a-%1.txt -ErrorAction SilentlyContinue); $t=@(Get-Content C:\NWOAS\gpucov\k46a-%1.log -ErrorAction SilentlyContinue); $r=@(); foreach($s in 1792,2048,2304,2560,3072){ $x=@($l -match ('memladder2 d3d12 size_mib='+$s+' .*result=(PASS|FAIL)')); $v='NONE'; if($x.Count -and ($x[0] -match 'result=(PASS|FAIL)')){ $v=$Matches[1] }; $r+=($s.ToString()+':'+$v) }; $pid0=''; $b=@($l -match 'NWGP begin'); if($b.Count -and ($b[0] -match 'pid=(\d+)')){ $pid0=$Matches[1] }; $rh=@($t -match '^measure-cpu-pool-rehome'); $ok=@($rh -match ' hr=0x00000000 '); $rj=@($t -match '^reject-native-allocation'); 'K46a LEG %1 mode=%2 pid='+$pid0+' '+($r -join ',')+' rehome='+$rh.Count+' rehome_ok='+$ok.Count+' reject='+$rj.Count; $rh | Select-Object -Last 2 | ForEach-Object { 'K46a LEG %1 RH '+$_.Substring(0,[math]::Min(230,$_.Length)) }"
endlocal
powershell -NoProfile -Command "Start-Sleep -Seconds 5"
goto :eof
