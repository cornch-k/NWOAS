@echo off
echo GPUCOV K47b LEGS in a boot that started with Wom1PoolCpuVisibleTopDown=1 (set by K47t before the restart): off R x4 (off = T only, R = T + rehome 40 x 250 ms)
set D=C:\NWOAS\gpucov
set P=%D%\nwgpuprobe14-arm64.exe
powershell -NoProfile -Command "Get-CimInstance Win32_VideoController | ForEach-Object { 'K47b VC '+$_.Name+' | '+$_.DriverVersion+' | '+$_.Status }; 'K47b TopDown='+(Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters' -ErrorAction SilentlyContinue).Wom1PoolCpuVisibleTopDown"
if not exist %P% (echo K47b probe14 missing & echo GPUCOV K47b DONE & exit /b 0)
call :leg 1 off
call :leg 2 R
call :leg 3 off
call :leg 4 R
call :leg 5 off
call :leg 6 R
call :leg 7 off
call :leg 8 R
echo GPUCOV K47b DONE
exit /b 0
:leg
setlocal
del /q %D%\k47b-%1.txt %D%\k47b-%1.log 2>nul
set APPLE_AGX_UMD_REFUSALS_ONLY=1
set APPLE_AGX_UMD_TRACE_SYNC=1
set APPLE_AGX_UMD_TRACE_FILE=%D%\k47b-%1.log
if "%2"=="R" set APPLE_AGX_CPU_POOL_REHOME_ROUNDS=40
if "%2"=="R" set APPLE_AGX_CPU_POOL_REHOME_SLEEP_MS=250
%P% pool dxgi memladder2 ladder=1792,2048,2304,2560,3072 > %D%\k47b-%1.txt 2>&1
powershell -NoProfile -Command "$l=@(Get-Content C:\NWOAS\gpucov\k47b-%1.txt -ErrorAction SilentlyContinue); $t=@(Get-Content C:\NWOAS\gpucov\k47b-%1.log -ErrorAction SilentlyContinue); $r=@(); foreach($s in 1792,2048,2304,2560,3072){ $x=@($l -match ('memladder2 d3d12 size_mib='+$s+' .*result=(PASS|FAIL)')); $v='NONE'; if($x.Count -and ($x[0] -match 'result=(PASS|FAIL)')){ $v=$Matches[1] }; $r+=($s.ToString()+':'+$v) }; $pid0=''; $b=@($l -match 'NWGP begin'); if($b.Count -and ($b[0] -match 'pid=(\d+)')){ $pid0=$Matches[1] }; $rh=@($t -match '^measure-cpu-pool-rehome'); $ok=@($rh -match ' hr=0x00000000 '); $rj=@($t -match '^reject-native-allocation'); 'K47b LEG %1 mode=%2 pid='+$pid0+' '+($r -join ',')+' rehome='+$rh.Count+' rehome_ok='+$ok.Count+' reject='+$rj.Count; $rh | Select-Object -Last 3 | ForEach-Object { 'K47b LEG %1 RH '+$_.Substring(0,[math]::Min(230,$_.Length)) }"
endlocal
powershell -NoProfile -Command "Start-Sleep -Seconds 5"
goto :eof
