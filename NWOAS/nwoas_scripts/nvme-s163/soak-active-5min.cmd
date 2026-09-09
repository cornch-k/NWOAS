@echo off
chcp 65001 >nul
echo === S163 FIVE MINUTE CONTINUOUS READ SOAK BEGIN ===
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; $clock=[Diagnostics.Stopwatch]::StartNew(); $n=0; do { $n++; Write-Output ('S163_ACTIVE '+$n+' elapsed_ms='+$clock.ElapsedMilliseconds); & D:\DISKREAD.EXE; if ($LASTEXITCODE -ne 0) { exit 12 } } while ($clock.Elapsed.TotalSeconds -lt 300); Write-Output ('S163_ACTIVE_PASS samples='+$n+' elapsed_ms='+$clock.ElapsedMilliseconds)"
if errorlevel 1 exit /b %errorlevel%
echo === S163 FIVE MINUTE CONTINUOUS READ SOAK PASS ===
exit /b 0
