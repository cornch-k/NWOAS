@echo off
powershell -NoProfile -Command "Write-Output ('BEFORE UTC='+[DateTime]::UtcNow.ToString('o')); Set-Date -Date ([DateTime]::Parse('2026-09-09T18:55:29Z').ToLocalTime()) -ErrorAction Stop; Get-AuthenticodeSignature -LiteralPath 'C:\NWOAS-BENCH\Cinebench2026\Cinebench.exe' | Select Status,StatusMessage | Format-List"
exit /b %errorlevel%
