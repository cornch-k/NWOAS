@echo off
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; Get-ChildItem -LiteralPath 'C:\Users\Public\Documents\NWOAS-BENCH' -Filter 'cb-s191*' | ForEach-Object { Write-Output ('FILE='+$_.Name+' bytes='+$_.Length); Get-Content -LiteralPath $_.FullName; Get-FileHash -LiteralPath $_.FullName }; Get-Process -Name Cinebench -ErrorAction SilentlyContinue | Select Id,CPU,SessionId; exit 0"
exit /b %errorlevel%
