@echo off
echo GPUCOV K13 CB26 TRACE CHUNK 3 of 4 (non-caps lines, 400 per chunk, pid/tid/qpc removed)
powershell -NoProfile -Command "$t='C:\NWOAS\gpucov\cb26-umd.log'; if(!(Test-Path $t)){'K13 NO TRACE';exit}; $l=@(Get-Content $t | Where-Object { $_ -notmatch '^measure-caps-d3d12-(format|msaa)' } | ForEach-Object { $_ -replace ' pid=\d+ tid=\d+ session=\d+ qpc=\d+','' }); 'K13 chunk=3 of_lines='+$l.Count; $l | Select-Object -Skip 800 -First 400 | ForEach-Object { 'K13 '+$_ }"
echo GPUCOV K13 DONE
exit /b 0
