@echo off
echo GPUCOV K9 TEXTURE/UPLOAD/BLEND (nwgpuprobe2 tex): driver default, driver + LEVEL_UPLOAD=0, driver + CPU_POOL_MAP=0, WARP reference
set D=C:\NWOAS\gpucov
set APPLE_AGX_UMD_TRACE_FILE=%D%\tex-umd.log
set APPLE_AGX_UMD_REFUSALS_ONLY=1
del /q %D%\tex-*.txt %D%\tex-umd.log 2>nul
%D%\nwgpuprobe2-arm64.exe tex > %D%\tex-default.txt 2>&1
set APPLE_AGX_UMD_TRACE_FILE=
set APPLE_AGX_D3D12_LEVEL_UPLOAD=0
%D%\nwgpuprobe2-arm64.exe tex > %D%\tex-nolevel.txt 2>&1
set APPLE_AGX_D3D12_LEVEL_UPLOAD=
set APPLE_AGX_D3D12_CPU_POOL_MAP=0
%D%\nwgpuprobe2-arm64.exe tex > %D%\tex-nopoolmap.txt 2>&1
set APPLE_AGX_D3D12_CPU_POOL_MAP=
%D%\nwgpuprobe2-arm64.exe tex warp > %D%\tex-warp.txt 2>&1
powershell -NoProfile -Command "$d='C:\NWOAS\gpucov'; foreach($k in 'default','nolevel','nopoolmap','warp'){ $f=Join-Path $d ('tex-'+$k+'.txt'); if(!(Test-Path $f)){'K9 '+$k+' NO OUTPUT';continue}; $l=Get-Content $f; 'K9 '+$k+' pass='+@($l -match 'result=PASS').Count+' fail='+@($l -match 'result=FAIL').Count; $l | Where-Object { $_ -match 'device |pipeline|result=FAIL|frame=|removed|TIMEOUT' } | Select-Object -First 30 | ForEach-Object { 'K9 '+$k+' '+$_ } }; $t=Join-Path $d 'tex-umd.log'; if(Test-Path $t){ $l=Get-Content $t; 'K9 TRACE lines='+$l.Count; $l | Where-Object { $_ -match '^(reject-|measure-d3d12-owner|measure-d3d12-level-upload hr=0x8|measure-d3d12-heap(-refused)? hr=0x8)' } | Select-Object -First 20 | ForEach-Object { 'K9 TR '+$_ } }"
echo GPUCOV K9 DONE
exit /b 0
