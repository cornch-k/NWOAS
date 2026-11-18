@echo off
echo GPUCOV K6 ENGINES / MEMORY COUNTERS / WMI (what Task Manager shows)
powershell -NoProfile -ExecutionPolicy Bypass -File C:\NWOAS\gpucov\engines.ps1 > C:\NWOAS\gpucov\engines.txt 2>&1
powershell -NoProfile -Command "Get-Content C:\NWOAS\gpucov\engines.txt | Select-Object -First 60 | ForEach-Object { 'K6 '+$_ }"
echo GPUCOV K6 DONE
exit /b 0
