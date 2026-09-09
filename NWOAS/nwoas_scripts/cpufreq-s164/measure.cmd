@echo off
chcp 65001 >nul
powershell -NoProfile -Command "1..5 | ForEach-Object { Write-Output ('S164_RUN '+$_); & C:\NWOAS-S164\PERCPU.EXE; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; Start-Sleep -Seconds 2 }"
exit /b %errorlevel%
