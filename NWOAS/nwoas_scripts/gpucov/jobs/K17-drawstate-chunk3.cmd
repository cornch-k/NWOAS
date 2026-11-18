@echo off
echo GPUCOV K17 D3D11 STATE TRACE CHUNK 3 of 5 (blend/raster/dsa/shader-sig/shader/drawstate lines, 400 per chunk; analyse on the Mac: gpucov/analyze-drawstate.py)
powershell -NoProfile -Command "$t='C:\NWOAS\gpucov\cb26-umd.log'; if(!(Test-Path $t)){'K17 NO TRACE';exit}; $l=@(Get-Content $t | Where-Object { $_ -match '^measure-d3d11-(blend|raster|dsa|shader-sig|shader |drawstate)' } | ForEach-Object { $_ -replace ' pid=\d+ tid=\d+ session=\d+ qpc=\d+','' }); 'K17 chunk=3 of_lines='+$l.Count; $l | Select-Object -Skip 800 -First 400 | ForEach-Object { 'K17 '+$_ }"
echo GPUCOV K17 DONE
exit /b 0
