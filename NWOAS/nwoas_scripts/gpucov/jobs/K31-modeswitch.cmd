@echo off
echo GPUCOV K31 RUNTIME MODE CHANGE (nwgpuprobe6 modeswitch: the swapchain set at the current mode, at another listed mode via ChangeDisplaySettingsEx, then after restoring it), our driver with the UMD refusal trace, user session
set D=C:\NWOAS\gpucov
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
> %D%\k31.cmd echo @echo off
>> %D%\k31.cmd echo set APPLE_AGX_UMD_TRACE_FILE=%D%\k31-umd.log
>> %D%\k31.cmd echo set APPLE_AGX_UMD_REFUSALS_ONLY=1
>> %D%\k31.cmd echo %D%\nwgpuprobe6-arm64.exe modeswitch ^> %D%\k31-driver.txt 2^>^&1
>> %D%\k31.cmd echo echo done^> %D%\k31-done.txt
del /q %D%\k31-done.txt %D%\k31-*.txt %D%\k31-umd.log 2>nul
powershell -NoProfile -Command "$t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'WAIT explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K31 -Cmd %D%\k31.cmd -Done %D%\k31-done.txt -Timeout 420
powershell -NoProfile -Command "foreach($k in 'driver'){ $f='C:\NWOAS\gpucov\k31-'+$k+'.txt'; if(!(Test-Path $f)){'K31 '+$k+' NO OUTPUT';continue}; $l=Get-Content $f; 'K31 '+$k+' pass='+@($l -match 'result=PASS').Count+' fail='+@($l -match 'result=FAIL').Count; $l | Where-Object { $_ -match 'd3d11ui (modeswitch|blt|flip)' -and $_ -notmatch 'listed=' } | ForEach-Object { 'K31 '+$k+' '+$_ } }"
powershell -NoProfile -Command "$t='C:\NWOAS\gpucov\k31-umd.log'; if(!(Test-Path $t)){'K31 NO TRACE';exit}; $l=Get-Content $t; 'K31 trace lines='+$l.Count; $l | Where-Object { $_ -match 'dxgi|Blt|present' -and $_ -match '^(measure-|reject-)' } | Select-Object -First 60 | ForEach-Object { 'K31 T '+$_.Substring(0,[math]::Min(300,$_.Length)) }; $l | Where-Object { $_ -match '^reject-' -and $_ -notmatch 'dxgi|Blt|present' } | Group-Object { ($_ -split ' ')[0] } | ForEach-Object { 'K31 R '+$_.Name+' x'+$_.Count }"
echo GPUCOV K31 DONE
exit /b 0
