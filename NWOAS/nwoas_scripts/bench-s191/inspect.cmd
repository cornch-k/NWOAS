@echo off
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; Write-Output ('UTC='+[DateTime]::UtcNow.ToString('o')); Get-Content -LiteralPath 'C:\Users\Public\Documents\NWOAS-BENCH\cb-s191.result'; if(Test-Path 'C:\Users\Public\Documents\NWOAS-BENCH\cb-s191.out'){Get-Content -LiteralPath 'C:\Users\Public\Documents\NWOAS-BENCH\cb-s191.out' -Tail 5}; Get-Process -Name Cinebench -ErrorAction SilentlyContinue | Select Id,SessionId,CPU,WorkingSet64 | Format-List; Get-PSDrive C | Select Free | Format-List; exit 0"
exit /b %errorlevel%
