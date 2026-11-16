@echo off
echo S248 D45 TRACE FAIL
powershell -NoProfile -Command "$ex=@(Get-Process explorer -ErrorAction SilentlyContinue | ForEach-Object { $_.Id }); 'EXPLORER '+($ex -join ','); $l=Get-Content C:\NWOAS\umd-trace.log -Tail 30000; 'TAIL '+$l.Count; $f=$l | Where-Object { $_ -match 'hr=0x8[0-9a-f]{7}' -and $_ -notmatch 'getcaps hr=0x80004001|retrieve-subobject|mpo-present1|gpuva-map-time hr=0x8000000a' }; 'FAILS '+$f.Count; $f | ForEach-Object { ($_ -split ' ')[0]+' '+($_ -split ' ')[1] } | Group-Object | Sort-Object Count -Descending | Select-Object -First 15 | ForEach-Object { ''+$_.Count+' '+$_.Name }; $f | Where-Object { $_ -notmatch 'reject-cxx' } | Select-Object -Last 12"
exit /b 0
