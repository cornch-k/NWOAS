@echo off
echo GPUCOV K2b PROBE arm64 d3d12+present with APPLE_AGX_D3D12_CPU_POOL_MAP=0 and UMD trace (A/B for P23 on 2758)
set D=C:\NWOAS\gpucov
> %D%\k2b.cmd echo @echo off
>> %D%\k2b.cmd echo set APPLE_AGX_D3D12_CPU_POOL_MAP=0
>> %D%\k2b.cmd echo set APPLE_AGX_UMD_TRACE_FILE=%D%\probe-umd.log
>> %D%\k2b.cmd echo set APPLE_AGX_UMD_REFUSALS_ONLY=1
>> %D%\k2b.cmd echo %D%\nwgpuprobe-arm64.exe d3d12 present ^> %D%\probe-b.txt 2^>^&1
>> %D%\k2b.cmd echo echo done^> %D%\k2b-done.txt
del /q %D%\probe-b.txt %D%\probe-umd.log 2>nul
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K2b -Cmd %D%\k2b.cmd -Done %D%\k2b-done.txt -Timeout 300
powershell -NoProfile -Command "$d='C:\NWOAS\gpucov'; if(Test-Path $d\probe-b.txt){ Get-Content $d\probe-b.txt | Where-Object { $_ -notmatch ' heap kind=| committed_buffer kind=' -or $_ -notmatch 'hr=0x00000000' } | Select-Object -First 60 | ForEach-Object {'K2b '+$_} }; $t=Join-Path $d 'probe-umd.log'; if(Test-Path $t){ $l=Get-Content $t; 'K2b TRACE owner='+(@($l -match '^measure-d3d12-owner ') -join ' | ')+' poolmap='+@($l -match '^reject-cpu-pool-map').Count; $l | Where-Object { $_ -match '^reject-' } | Select-Object -First 8 | ForEach-Object {'K2b TR '+$_} }"
echo GPUCOV K2b DONE
exit /b 0
