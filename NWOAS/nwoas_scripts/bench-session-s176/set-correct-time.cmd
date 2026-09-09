@echo off
powershell -NoProfile -Command "Write-Output ('BEFORE UTC='+[DateTime]::UtcNow.ToString('o')); Set-Date -Date ([DateTime]::Parse('2026-09-09T18:47:49Z').ToLocalTime()) -ErrorAction Stop; Write-Output ('AFTER UTC='+[DateTime]::UtcNow.ToString('o'))"
exit /b %errorlevel%
