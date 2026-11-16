@echo off
echo S248 D20 DWM 4372 LIFE
powershell -NoProfile -Command "$l=Get-Content C:\NWOAS\umd-trace.log | Where-Object { $_ -match 'pid=4372 ' -and $_ -notmatch 'measure-g4-phase|measure-getcaps|measure-getversions|measure-ddi-time' }; 'N '+$l.Count; $l | ForEach-Object { $_ -replace ' pid=4372','' -replace ' session=1','' } | Select-Object -Last 140"
powershell -NoProfile -Command "$l=Get-Content C:\NWOAS\umd-trace.log | Where-Object { $_ -match 'pid=4372 ' }; 'KINDS'; $l | ForEach-Object { ($_ -split ' ')[0]+' '+($_ -split ' ')[1] } | Group-Object | Sort-Object Count -Descending | ForEach-Object { ''+$_.Count+' '+$_.Name }"
exit /b 0
