@echo off
echo GPUCOV K56 PSVARY (probe17, no driver change): 12 cases of values passed from the vertex shader to the pixel shader (scale 1 and 100, linear and noperspective, pixel shader reading all inputs or only the last one, and the conic-gradient angle), each drawn 64x64 and compared with CPU values. Runs: arm64 on the AGX driver, x64 on the AGX driver, arm64 on WARP (same machine reference). PASS = every psvary end line says result=PASS
set D=C:\NWOAS\gpucov
if not exist %D%\nwgpuprobe17-arm64.exe (echo K56 probe17 missing - deliver gpucov21 first & echo GPUCOV K56 DONE & exit /b 0)
powershell -NoProfile -Command "Get-CimInstance Win32_VideoController | ForEach-Object { 'K56 VC '+$_.Name+' | '+$_.DriverVersion+' | '+$_.Status }"
%D%\nwgpuprobe17-arm64.exe psvary > %D%\k56-arm64.txt 2>&1
%D%\nwgpuprobe17-x64.exe psvary > %D%\k56-x64.txt 2>&1
%D%\nwgpuprobe17-arm64.exe psvary warp > %D%\k56-warp.txt 2>&1
powershell -NoProfile -Command "foreach($n in 'arm64','x64','warp'){ $l=@(Get-Content ('C:\NWOAS\gpucov\k56-'+$n+'.txt') -ErrorAction SilentlyContinue); $l | Where-Object { $_ -match 'psvary (begin|end|case|map)' } | Select-Object -First 400 | ForEach-Object { 'K56 '+$n+' '+$_ }; 'K56 '+$n+' SUMMARY lines='+$l.Count+' pass='+@($l -match 'psvary case=.*result=PASS').Count+' fail='+@($l -match 'psvary case=.*result=FAIL').Count+' failed='+((@($l -match 'psvary case=.*result=FAIL') | ForEach-Object { if($_ -match 'case=(\S+) (bad)'){ $Matches[1]+'/'+$Matches[2] } }) -join ',') }"
echo GPUCOV K56 DONE
exit /b 0
