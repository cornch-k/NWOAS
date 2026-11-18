@echo off
echo GPUCOV K25 D3D11 UI PROBE (nwgpuprobe5 d3d11ui: SM4 shaders through the D3D11 frontend, cull, dynamic buffers, blend, scissor, textures, offscreen, blt-model swapchains) on our driver and on WARP, user session
set D=C:\NWOAS\gpucov
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
> %D%\k25.cmd echo @echo off
>> %D%\k25.cmd echo %D%\nwgpuprobe5-arm64.exe d3d11ui ^> %D%\d3d11ui-driver.txt 2^>^&1
>> %D%\k25.cmd echo %D%\nwgpuprobe5-arm64.exe d3d11ui warp ^> %D%\d3d11ui-warp.txt 2^>^&1
>> %D%\k25.cmd echo echo done^> %D%\k25-done.txt
del /q %D%\k25-done.txt %D%\d3d11ui-*.txt 2>nul
powershell -NoProfile -Command "$t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'WAIT explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K25 -Cmd %D%\k25.cmd -Done %D%\k25-done.txt -Timeout 200
powershell -NoProfile -Command "foreach($k in 'driver','warp'){ $f='C:\NWOAS\gpucov\d3d11ui-'+$k+'.txt'; if(!(Test-Path $f)){'K25 '+$k+' NO OUTPUT';continue}; $l=Get-Content $f; 'K25 '+$k+' pass='+@($l -match 'result=PASS').Count+' fail='+@($l -match 'result=FAIL').Count; $l | Where-Object { $_ -match 'd3d11ui ' } | ForEach-Object { 'K25 '+$k+' '+$_ } }"
echo GPUCOV K25 DONE
exit /b 0
