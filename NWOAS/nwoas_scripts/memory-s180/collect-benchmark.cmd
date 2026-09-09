@echo off
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; $d='C:\Users\Public\Documents\NWOAS-BENCH'; Get-Content -LiteralPath ($d+'\cb-20260909-192841.out'); Get-Content -LiteralPath ($d+'\cb-20260909-192841.err'); Get-Content -LiteralPath ($d+'\cb-user-s176-v2.log') -Tail 12; Get-Process -Name Cinebench -ErrorAction SilentlyContinue | Select Id,CPU,SessionId; Get-FileHash -LiteralPath ($d+'\cb-20260909-192841.out'),($d+'\cb-20260909-192841.err'),($d+'\cb-user-s176-v2.log') -Algorithm SHA256 | Format-List"
exit /b %errorlevel%
