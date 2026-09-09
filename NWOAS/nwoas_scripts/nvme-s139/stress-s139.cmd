@echo off
echo === S139 8CPU STRESS BEGIN %date% %time% ===
wmic cpu get NumberOfCores,NumberOfLogicalProcessors,CurrentClockSpeed,MaxClockSpeed /format:list
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "$jobs=1..8|ForEach-Object { Start-Job -ScriptBlock { $sw=[Diagnostics.Stopwatch]::StartNew(); [uint64]$x=1; while($sw.Elapsed.TotalSeconds -lt 20) { for($i=0;$i -lt 200000;$i++) { $x=($x*1664525+1013904223) -band 0xffffffff } }; $x } }; $values=$jobs|Wait-Job|Receive-Job; $jobs|Remove-Job -Force; Write-Output ('completed_workers='+@($values).Count); if(@($values).Count -ne 8) { exit 21 }"
if errorlevel 1 exit /b %errorlevel%
echo === S139 CPU STRESS PASS ===
echo === S139 READ-ONLY DISK STRESS ===
winsat disk -seq -read -drive c
if errorlevel 1 exit /b %errorlevel%
echo === S139 DISK STRESS PASS ===
wmic cpu get LoadPercentage,NumberOfCores,NumberOfLogicalProcessors /format:list
wevtutil qe System /q:"*[System[(EventID=1001 or EventID=129 or EventID=153)]]" /rd:true /c:5 /f:text
echo === S139 STRESS COMPLETE ===
