@echo off
echo S248 D39 TWINUI
powershell -NoProfile -Command "foreach($f in 'C:\Windows\System32\twinui.pcshell.dll'){ $b=[IO.File]::ReadAllBytes($f); $pe=[BitConverter]::ToInt32($b,0x3c); $ts=[BitConverter]::ToUInt32($b,$pe+8); $soi=[BitConverter]::ToUInt32($b,$pe+24+56); ''+$f+' TS='+$ts.ToString('X8')+' SOI='+$soi.ToString('X')+' LEN='+$b.Length }; $e=@(Get-WinEvent -FilterHashtable @{LogName='Application';Id=1000;StartTime=(Get-Date).AddMinutes(-3)} -ErrorAction SilentlyContinue); 'RECENT EXPLORER CRASHES '+$e.Count; Get-Date -Format HH:mm:ss"
powershell -NoProfile -Command "$l=Get-Content C:\NWOAS\umd-trace.log -Tail 4000; $l | Where-Object { $_ -match '^reject|hr=0x8[0-9a-f]{7}' -and $_ -notmatch 'getcaps|retrieve-subobject|mpo-present1|reject-cxx' } | Select-Object -Last 15"
exit /b 0
