@echo off
echo GPUCOV K4b CINEBENCH UI with APPLE_AGX_D3D12_CPU_POOL_MAP=0 (this process only; A/B for P23 on 2758)
set D=C:\NWOAS\gpucov
> %D%\k4b.cmd echo @echo off
>> %D%\k4b.cmd echo set APPLE_AGX_D3D12_CPU_POOL_MAP=0
>> %D%\k4b.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\cb26ui.ps1 -Wait 75
del /q %D%\cb26-done.txt 2>nul
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K4b -Cmd %D%\k4b.cmd -Done %D%\cb26-done.txt -Timeout 200
powershell -NoProfile -Command "$d='C:\NWOAS\gpucov'; if(Test-Path $d\cb26-stat.txt){Get-Content $d\cb26-stat.txt | ForEach-Object {'K4b '+$_}}; if(Test-Path $d\cb26-ui.txt){ Get-Content $d\cb26-ui.txt | Select-String -Pattern 'CRITICAL|rawport|OUTOFMEMORY' | Select-Object -First 10 | ForEach-Object {'K4b ui '+$_.Line} }; $t=Join-Path $d 'cb26-umd.log'; if(Test-Path $t){ $l=Get-Content $t; 'K4b TRACE lines='+$l.Count+' owner='+(@($l -match '^measure-d3d12-owner ') -join ' | ')+' heap_ok='+@($l -match '^measure-d3d12-heap hr=0x00000000').Count+' heap_fail='+@($l -match '^measure-d3d12-heap(-refused)? hr=0x8').Count+' poolmap='+@($l -match '^reject-cpu-pool-map').Count+' backend_fail='+@($l -match '^reject-batch hr=\S+ \S+ \S+ \S+ \S+ 00000004').Count; $l | Where-Object { $_ -match '^reject-|^measure-d3d12-heap(-refused)? hr=0x8' } | Select-Object -First 15 | ForEach-Object {'K4b TR '+$_} }"
echo GPUCOV K4b DONE
exit /b 0
