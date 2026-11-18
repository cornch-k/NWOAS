@echo off
echo GPUCOV K8 CB26 UMD TRACE (owner, screen, refusals; bounded)
powershell -NoProfile -Command "$t='C:\NWOAS\gpucov\cb26-umd.log'; if(!(Test-Path $t)){'K8 NO TRACE';exit}; $l=Get-Content $t; 'K8 lines='+$l.Count; $l | Where-Object { $_ -match '^(measure-d3d12-owner|g4-screen-initialize|measure-d3d12-upload-ring-init|reject-|measure-d3d12-heap|measure-cpu-pool-rehome|measure-d3d12-level-upload hr=0x8|device-)' } | Select-Object -First 150 | ForEach-Object { 'K8 '+$_ }"
echo GPUCOV K8 DONE
exit /b 0
