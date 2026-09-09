@echo off
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; Get-Content -LiteralPath 'C:\Users\Public\Documents\NWOAS-BENCH\cb-user-s176-v2.log' -Tail 8; Get-ChildItem -LiteralPath 'C:\Users\Public\Documents\NWOAS-BENCH' -Filter 'cb-20260909*.out' | ForEach-Object {Write-Output $_.Name; Get-Content -LiteralPath $_.FullName -Tail 5}"
exit /b %errorlevel%
