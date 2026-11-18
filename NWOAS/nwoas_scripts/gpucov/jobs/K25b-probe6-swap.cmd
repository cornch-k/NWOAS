@echo off
echo GPUCOV K25b SWAPCHAIN PRESENTS (nwgpuprobe6 d3d11ui: blt_swapchain with ResizeBuffers, blt_noresize stretched, flip_swapchain) on our driver with the UMD refusal trace, and on WARP, user session
set D=C:\NWOAS\gpucov
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
> %D%\k25b.cmd echo @echo off
>> %D%\k25b.cmd echo set APPLE_AGX_UMD_TRACE_FILE=%D%\k25b-umd.log
>> %D%\k25b.cmd echo set APPLE_AGX_UMD_REFUSALS_ONLY=1
>> %D%\k25b.cmd echo %D%\nwgpuprobe6-arm64.exe d3d11ui ^> %D%\k25b-driver.txt 2^>^&1
>> %D%\k25b.cmd echo set APPLE_AGX_UMD_TRACE_FILE=
>> %D%\k25b.cmd echo %D%\nwgpuprobe6-arm64.exe d3d11ui warp ^> %D%\k25b-warp.txt 2^>^&1
>> %D%\k25b.cmd echo echo done^> %D%\k25b-done.txt
del /q %D%\k25b-done.txt %D%\k25b-*.txt %D%\k25b-umd.log 2>nul
powershell -NoProfile -Command "$t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'WAIT explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K25b -Cmd %D%\k25b.cmd -Done %D%\k25b-done.txt -Timeout 240
powershell -NoProfile -Command "foreach($k in 'driver','warp'){ $f='C:\NWOAS\gpucov\k25b-'+$k+'.txt'; if(!(Test-Path $f)){'K25b '+$k+' NO OUTPUT';continue}; $l=Get-Content $f; 'K25b '+$k+' pass='+@($l -match 'result=PASS').Count+' fail='+@($l -match 'result=FAIL').Count; $l | Where-Object { $_ -match 'd3d11ui ' } | ForEach-Object { 'K25b '+$k+' '+$_ } }"
powershell -NoProfile -Command "$t='C:\NWOAS\gpucov\k25b-umd.log'; if(!(Test-Path $t)){'K25b NO TRACE';exit}; $l=Get-Content $t; 'K25b trace lines='+$l.Count; $l | Where-Object { $_ -match 'dxgi|Blt|present' -and $_ -match '^(measure-|reject-)' } | Select-Object -First 60 | ForEach-Object { 'K25b T '+$_.Substring(0,[math]::Min(300,$_.Length)) }; $l | Where-Object { $_ -match '^reject-' -and $_ -notmatch 'dxgi|Blt|present' } | Group-Object { ($_ -split ' ')[0] } | ForEach-Object { 'K25b R '+$_.Name+' x'+$_.Count }"
echo GPUCOV K25b DONE
exit /b 0
