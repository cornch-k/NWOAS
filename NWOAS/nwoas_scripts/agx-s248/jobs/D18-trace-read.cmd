@echo off
echo S248 D18 TRACE READ
powershell -NoProfile -Command "$f='C:\NWOAS\umd-trace.log'; if(!(Test-Path $f)){'NO TRACE';exit}; $l=Get-Content $f; 'LINES '+$l.Count; 'FIRST'; $l|Select-Object -First 25; 'MATCHES'; $l|Select-String -Pattern 'fail|error|refus|reject|invalid|E_|0x8[0-9a-f]{7}|lost|removed|denied|unsupported' -CaseSensitive:$false|Select-Object -First 80|ForEach-Object{''+$_.LineNumber+': '+$_.Line}; 'LAST'; $l|Select-Object -Last 60"
powershell -NoProfile -Command "Get-ChildItem C:\NWOAS | Select-Object Name,Length,LastWriteTime | Format-Table -AutoSize | Out-String -Width 200"
exit /b 0
