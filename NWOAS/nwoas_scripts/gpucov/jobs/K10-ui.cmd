@echo off
echo GPUCOV K10 UI DRAW FEATURES (nwgpuprobe3 ui: driver + WARP reference) and PRESENT variants (main, child window, DirectComposition; user session)
set D=C:\NWOAS\gpucov
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
%D%\nwgpuprobe3-arm64.exe ui > %D%\ui-driver.txt 2>&1
%D%\nwgpuprobe3-arm64.exe ui warp > %D%\ui-warp.txt 2>&1
> %D%\k10.cmd echo @echo off
>> %D%\k10.cmd echo %D%\nwgpuprobe3-arm64.exe present ^> %D%\ui-present.txt 2^>^&1
>> %D%\k10.cmd echo echo done^> %D%\k10-done.txt
del /q %D%\ui-present.txt 2>nul
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K10 -Cmd %D%\k10.cmd -Done %D%\k10-done.txt -Timeout 180
powershell -NoProfile -Command "$d='C:\NWOAS\gpucov'; foreach($k in 'driver','warp','present'){ $f=Join-Path $d ('ui-'+$k+'.txt'); if(!(Test-Path $f)){'K10 '+$k+' NO OUTPUT';continue}; $l=Get-Content $f; 'K10 '+$k+' pass='+@($l -match 'result=PASS|screen_check=PASS').Count+' fail='+@($l -match 'result=FAIL|screen_check=FAIL').Count; $l | Where-Object { $_ -match 'device |setup|result=|screen_check|swapchain|removed|TIMEOUT' } | Select-Object -First 25 | ForEach-Object { 'K10 '+$k+' '+$_ } }"
echo GPUCOV K10 DONE
exit /b 0
