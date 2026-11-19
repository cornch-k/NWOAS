@echo off
echo GPUCOV K46z TOPDOWN OFF: Wom1PoolCpuVisibleTopDown=0 + GPU device restart, then 2 ladders in mode off (same-boot control for K46t)
set D=C:\NWOAS\gpucov
set P=%D%\nwgpuprobe14-arm64.exe
if not exist %P% (echo K46z probe14 missing & echo GPUCOV K46z DONE & exit /b 0)
set K="HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters"
reg add %K% /v Wom1PoolCpuVisibleTopDown /t REG_DWORD /d 0 /f >nul
reg add %K% /v G3Armed /t REG_DWORD /d 1 /f >nul
pnputil /restart-device "ACPI\APPL0002\0" >nul
ping -n 46 127.0.0.1 >nul
powershell -NoProfile -Command "Get-PnpDevice -InstanceId 'ACPI\APPL0002\0' | ForEach-Object { 'K46z DEV '+$_.Status+' '+$_.Problem }; 'K46z TopDown='+(Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters').Wom1PoolCpuVisibleTopDown"
call :leg 1 off
call :leg 2 off
echo GPUCOV K46z DONE
exit /b 0
:leg
setlocal
del /q %D%\k46z-%1.txt %D%\k46z-%1.log 2>nul
set APPLE_AGX_UMD_REFUSALS_ONLY=1
set APPLE_AGX_UMD_TRACE_SYNC=1
set APPLE_AGX_UMD_TRACE_FILE=%D%\k46z-%1.log
if "%2"=="R" set APPLE_AGX_CPU_POOL_REHOME_ROUNDS=16
if "%2"=="R" set APPLE_AGX_CPU_POOL_REHOME_SLEEP_MS=250
if "%2"=="P" set APPLE_AGX_CPU_POOL_PRIORITY=2
%P% pool dxgi memladder2 ladder=1792,2048,2304,2560,3072 > %D%\k46z-%1.txt 2>&1
powershell -NoProfile -Command "$l=@(Get-Content C:\NWOAS\gpucov\k46z-%1.txt -ErrorAction SilentlyContinue); $t=@(Get-Content C:\NWOAS\gpucov\k46z-%1.log -ErrorAction SilentlyContinue); $r=@(); foreach($s in 1792,2048,2304,2560,3072){ $x=@($l -match ('memladder2 d3d12 size_mib='+$s+' .*result=(PASS|FAIL)')); $v='NONE'; if($x.Count -and ($x[0] -match 'result=(PASS|FAIL)')){ $v=$Matches[1] }; $r+=($s.ToString()+':'+$v) }; $pid0=''; $b=@($l -match 'NWGP begin'); if($b.Count -and ($b[0] -match 'pid=(\d+)')){ $pid0=$Matches[1] }; $rh=@($t -match '^measure-cpu-pool-rehome'); $ok=@($rh -match ' hr=0x00000000 '); $rj=@($t -match '^reject-native-allocation'); 'K46z LEG %1 mode=%2 pid='+$pid0+' '+($r -join ',')+' rehome='+$rh.Count+' rehome_ok='+$ok.Count+' reject='+$rj.Count; $rh | Select-Object -Last 2 | ForEach-Object { 'K46z LEG %1 RH '+$_.Substring(0,[math]::Min(230,$_.Length)) }"
endlocal
powershell -NoProfile -Command "Start-Sleep -Seconds 5"
goto :eof
