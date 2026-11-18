@echo off
echo CB26 C4 READ RESULTS
powershell -NoProfile -Command "foreach($f in 'multi','single'){ '=== '+$f; if(Test-Path \"C:\NWOAS\cb26\$f.txt\"){ Get-Content \"C:\NWOAS\cb26\$f.txt\" | Select-String -Pattern '^CB |Running' | ForEach-Object { $_.Line } } else { 'NO OUTPUT' } }; 'DONEFILE '+(Test-Path 'C:\NWOAS\cb26\done.txt'); 'RUNNING '+((Get-Process Cinebench -ErrorAction SilentlyContinue | Measure-Object).Count)"
echo CB26 C4 DONE
exit /b 0
