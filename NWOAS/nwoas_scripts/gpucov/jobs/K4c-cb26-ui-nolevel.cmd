@echo off
echo GPUCOV K4c CINEBENCH UI with CPU_POOL_MAP=0 and LEVEL_UPLOAD=0 (texture uploads by the staging blit instead of the level ring)
set D=C:\NWOAS\gpucov
> %D%\k4c.cmd echo @echo off
>> %D%\k4c.cmd echo set APPLE_AGX_D3D12_CPU_POOL_MAP=0
>> %D%\k4c.cmd echo set APPLE_AGX_D3D12_LEVEL_UPLOAD=0
>> %D%\k4c.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\cb26ui.ps1 -Wait 75
del /q %D%\cb26-done.txt 2>nul
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K4c -Cmd %D%\k4c.cmd -Done %D%\cb26-done.txt -Timeout 200
powershell -NoProfile -Command "$d='C:\NWOAS\gpucov'; if(Test-Path $d\cb26-stat.txt){Get-Content $d\cb26-stat.txt | ForEach-Object {'K4c '+$_}}; $t=Join-Path $d 'cb26-umd.log'; if(Test-Path $t){ $l=Get-Content $t; 'K4c TRACE lines='+$l.Count+' level_ok='+@($l -match '^measure-d3d12-level-upload hr=0x00000000').Count+' level_fail='+@($l -match '^measure-d3d12-level-upload hr=0x8').Count+' rejects='+@($l | Where-Object { $_ -match '^reject-' -and $_ -notmatch '^reject-cxx' }).Count; $l | Where-Object { $_ -match '^reject-' -and $_ -notmatch 'reject-cxx' } | Select-Object -First 12 | ForEach-Object {'K4c TR '+$_} }"
echo GPUCOV K4c DONE
exit /b 0
