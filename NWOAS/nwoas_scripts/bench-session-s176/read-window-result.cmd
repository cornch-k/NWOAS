@echo off
powershell -NoProfile -Command "Get-Content -LiteralPath 'C:\Users\Public\Documents\NWOAS-BENCH\window-text-s176.log' -ErrorAction Stop; Get-Content -LiteralPath 'C:\Users\Public\Documents\NWOAS-BENCH\cb-user-s176.log' -Tail 5 -ErrorAction Stop"
exit /b %errorlevel%
