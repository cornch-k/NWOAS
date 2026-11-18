@echo off
echo GPUCOV K16 CINEBENCH UI with the D3D11 draw-state trace and the shader+signature dump (needs 2762); print with K17-drawstate-chunk1..5
set D=C:\NWOAS\gpucov
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
> %D%\k16.cmd echo @echo off
>> %D%\k16.cmd echo set APPLE_AGX_D3D_DRAW_STATE=1
>> %D%\k16.cmd echo set APPLE_AGX_D3D_SHADER_DUMP=1
>> %D%\k16.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\cb26ui.ps1 -Wait 75
del /q %D%\cb26-done.txt %D%\cb26-stat.txt %D%\cb26-umd.log 2>nul
powershell -NoProfile -Command "$t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'WAIT explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K16 -Cmd %D%\k16.cmd -Done %D%\cb26-done.txt -Timeout 200
if not exist %D%\cb26-done.txt (echo K16 RETRY & powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K16 -Cmd %D%\k16.cmd -Done %D%\cb26-done.txt -Timeout 200)
powershell -NoProfile -Command "$t='C:\NWOAS\gpucov\cb26-umd.log'; if(!(Test-Path $t)){'K16 NO TRACE';exit}; $l=@(Get-Content $t); 'K16 lines='+$l.Count; foreach($k in 'blend','raster','dsa','shader-sig','drawstate','shader '){ 'K16 COUNT '+$k+' '+@($l -match ('^measure-d3d11-'+$k)).Count }; if(Test-Path C:\NWOAS\gpucov\dxbc){'K16 DXBC files='+@(Get-ChildItem C:\NWOAS\gpucov\dxbc -File).Count}"
echo GPUCOV K16 DONE
exit /b 0
