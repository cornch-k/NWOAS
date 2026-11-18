@echo off
del /q C:\NWOAS\gpucov\snap.ps1 C:\NWOAS\gpucov\k-snap.cmd C:\NWOAS\gpucov\snap-log.txt 2>nul
if exist C:\NWOAS\gpucov\snap.ps1 (echo G3 LEFT) else (echo G3 REMOVED)
exit /b 0
