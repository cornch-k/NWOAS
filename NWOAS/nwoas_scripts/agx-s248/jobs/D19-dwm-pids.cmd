@echo off
echo S248 D19 DWM PIDS
powershell -NoProfile -Command "Get-WinEvent -FilterHashtable @{LogName='Application';Id=1000} -MaxEvents 30 -ErrorAction SilentlyContinue | ForEach-Object { ''+$_.TimeCreated.ToString('HH:mm:ss')+' | '+(($_.Properties|Select-Object -First 12|ForEach-Object{$_.Value}) -join ' ; ') }"
powershell -NoProfile -Command "$l=Get-Content C:\NWOAS\umd-trace.log; $g=@{}; foreach($x in $l){ if($x -match 'pid=(\d+) tid=\d+ session=\d+ qpc=(\d+)'){ $p=$matches[1]; if(!$g[$p]){$g[$p]=@(0,$matches[2],0)}; $g[$p][0]++; $g[$p][2]=$matches[2] } }; 'PIDS'; $g.GetEnumerator()|Sort-Object {[int64]$_.Value[1]}|ForEach-Object{ $_.Key+' n='+$_.Value[0]+' first='+$_.Value[1]+' last='+$_.Value[2] }; 'CTX 225-300'; $l[224..299]"
exit /b 0
