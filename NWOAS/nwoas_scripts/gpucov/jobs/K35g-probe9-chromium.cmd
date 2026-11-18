@echo off
echo GPUCOV K35g CHROMIUM GPU INIT PROBE v3 (nwgpuprobe9 chromium: whole-frame Present x3, dirty-rect Present1 only after a whole frame on FLIP_SEQUENTIAL/SEQUENTIAL; the nwgpuprobe8 first-frame Present1 and FLIP_DISCARD dirty rects are informational lines -- DXGI refuses them on every adapter, VM 11-19 03:4x; same device/format/texture/MSAA/DComp tests as K35) on our driver with the UMD refusal trace and on WARP, user session. PASS = driver fail=0
set D=C:\NWOAS\gpucov
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
> %D%\k35g.cmd echo @echo off
>> %D%\k35g.cmd echo set APPLE_AGX_UMD_TRACE_FILE=%D%\k35g-umd.log
>> %D%\k35g.cmd echo set APPLE_AGX_UMD_REFUSALS_ONLY=1
>> %D%\k35g.cmd echo set APPLE_AGX_UMD_TRACE_SYNC=1
>> %D%\k35g.cmd echo %D%\nwgpuprobe9-arm64.exe chromium ^> %D%\k35g-driver.txt 2^>^&1
>> %D%\k35g.cmd echo set APPLE_AGX_UMD_TRACE_FILE=
>> %D%\k35g.cmd echo %D%\nwgpuprobe9-arm64.exe chromium warp ^> %D%\k35g-warp.txt 2^>^&1
>> %D%\k35g.cmd echo echo done^> %D%\k35g-done.txt
del /q %D%\k35g-done.txt %D%\k35g-*.txt %D%\k35g-umd.log 2>nul
powershell -NoProfile -Command "$t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'WAIT explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K35g -Cmd %D%\k35g.cmd -Done %D%\k35g-done.txt -Timeout 240
powershell -NoProfile -Command "foreach($k in 'driver','warp'){ $f='C:\NWOAS\gpucov\k35g-'+$k+'.txt'; if(!(Test-Path $f)){'K35g '+$k+' NO OUTPUT';continue}; $l=Get-Content $f; 'K35g '+$k+' pass='+@($l -match 'result=PASS').Count+' fail='+@($l -match 'result=FAIL').Count+' last='+$l[-1]; $l | Where-Object { $_ -match 'result=FAIL|summary|device |video_device|tearing|output_desc1|fmt_|swap_.*_present|informational|msaa_' } | ForEach-Object { 'K35g '+$k+' '+$_ } }"
powershell -NoProfile -Command "$t='C:\NWOAS\gpucov\k35g-umd.log'; if(!(Test-Path $t)){'K35g NO TRACE';exit}; $l=Get-Content $t; 'K35g trace lines='+$l.Count; $l | Where-Object { $_ -match '^reject-' } | Group-Object { ($_ -split ' ')[0] } | ForEach-Object { 'K35g R '+$_.Name+' x'+$_.Count }; $l | Where-Object { $_ -match '^reject-' } | Select-Object -First 40 | ForEach-Object { 'K35g RL '+$_.Substring(0,[math]::Min(300,$_.Length)) }"
echo GPUCOV K35g DONE
exit /b 0
