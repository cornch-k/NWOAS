@echo off
echo GPUCOV K54 PSMATH (probe16, no driver change): 32 pixel-shader expressions x ps_4_0/ps_5_0, each drawn 64x64 and compared with CPU values. Runs: arm64 on the AGX driver, x64 on the AGX driver, arm64 on WARP (same machine reference). PASS = every psmath end line says result=PASS
set D=C:\NWOAS\gpucov
if not exist %D%\nwgpuprobe16-arm64.exe (echo K54 probe16 missing - deliver gpucov20 first & echo GPUCOV K54 DONE & exit /b 0)
powershell -NoProfile -Command "Get-CimInstance Win32_VideoController | ForEach-Object { 'K54 VC '+$_.Name+' | '+$_.DriverVersion+' | '+$_.Status }"
%D%\nwgpuprobe16-arm64.exe psmath > %D%\k54-arm64.txt 2>&1
%D%\nwgpuprobe16-x64.exe psmath > %D%\k54-x64.txt 2>&1
%D%\nwgpuprobe16-arm64.exe psmath warp > %D%\k54-warp.txt 2>&1
powershell -NoProfile -Command "foreach($n in 'arm64','x64','warp'){ $l=@(Get-Content ('C:\NWOAS\gpucov\k54-'+$n+'.txt') -ErrorAction SilentlyContinue); $l | Where-Object { $_ -match 'psmath (begin|end)|result=FAIL|psmath map' } | Select-Object -First 400 | ForEach-Object { 'K54 '+$n+' '+$_ }; 'K54 '+$n+' SUMMARY lines='+$l.Count+' pass='+@($l -match 'psmath case=.*result=PASS').Count+' fail='+@($l -match 'psmath case=.*result=FAIL').Count+' failed='+((@($l -match 'psmath case=.*result=FAIL') | ForEach-Object { if($_ -match 'case=(\S+) target=(\S+)'){ $Matches[1]+'/'+$Matches[2] } }) -join ',') }"
echo GPUCOV K54 DONE
exit /b 0
