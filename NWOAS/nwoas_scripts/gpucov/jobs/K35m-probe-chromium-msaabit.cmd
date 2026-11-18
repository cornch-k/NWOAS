@echo off
echo GPUCOV K35m CHROMIUM GPU INIT PROBE with MSAA_CAPS=80 (bit only), (nwgpuprobe8 chromium, new device after a removal: device and QIs, CheckFeatureSupport, tearing, outputs/GetDesc1, formats, shared/keyed-mutex/NT-handle textures, depth, MSAA, flip/blt swapchains with tearing and waitable object, DComp) on our driver with the UMD refusal trace, and on WARP, user session
set D=C:\NWOAS\gpucov
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
> %D%\k35m.cmd echo @echo off
>> %D%\k35m.cmd echo set APPLE_AGX_UMD_TRACE_FILE=%D%\k35m-umd.log
>> %D%\k35m.cmd echo set APPLE_AGX_UMD_REFUSALS_ONLY=1
>> %D%\k35m.cmd echo set APPLE_AGX_UMD_TRACE_SYNC=1
>> %D%\k35m.cmd echo set APPLE_AGX_D3D_MSAA_CAPS=80
>> %D%\k35m.cmd echo %D%\nwgpuprobe8-arm64.exe chromium ^> %D%\k35m-driver.txt 2^>^&1
>> %D%\k35m.cmd echo set APPLE_AGX_UMD_TRACE_FILE=
>> %D%\k35m.cmd echo %D%\nwgpuprobe8-arm64.exe chromium warp ^> %D%\k35m-warp.txt 2^>^&1
>> %D%\k35m.cmd echo echo done^> %D%\k35m-done.txt
del /q %D%\k35m-done.txt %D%\k35m-*.txt %D%\k35m-umd.log 2>nul
powershell -NoProfile -Command "$t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'WAIT explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K35m -Cmd %D%\k35m.cmd -Done %D%\k35m-done.txt -Timeout 240
powershell -NoProfile -Command "foreach($k in 'driver','warp'){ $f='C:\NWOAS\gpucov\k35m-'+$k+'.txt'; if(!(Test-Path $f)){'K35m '+$k+' NO OUTPUT';continue}; $l=Get-Content $f; 'K35m '+$k+' pass='+@($l -match 'result=PASS').Count+' fail='+@($l -match 'result=FAIL').Count+' last='+$l[-1]; $l | Where-Object { $_ -match 'result=FAIL|summary|device |device_removed|video_device|tearing|output_desc1|fmt_|msaa_' } | ForEach-Object { 'K35m '+$k+' '+$_ } }"
powershell -NoProfile -Command "$t='C:\NWOAS\gpucov\k35m-umd.log'; if(!(Test-Path $t)){'K35m NO TRACE';exit}; $l=Get-Content $t; 'K35m trace lines='+$l.Count; $l | Where-Object { $_ -match '^reject-' } | Group-Object { ($_ -split ' ')[0] } | ForEach-Object { 'K35m R '+$_.Name+' x'+$_.Count }; $l | Where-Object { $_ -match '^reject-' } | Select-Object -First 40 | ForEach-Object { 'K35m RL '+$_.Substring(0,[math]::Min(300,$_.Length)) }"
echo GPUCOV K35m DONE
exit /b 0
