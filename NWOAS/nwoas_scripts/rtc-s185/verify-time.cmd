@echo off
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; Write-Output ('UTC='+[DateTime]::UtcNow.ToString('o')); Get-TimeZone | Select Id,BaseUtcOffset | Format-List; Get-AuthenticodeSignature -LiteralPath 'C:\NWOAS-BENCH\Cinebench2026\Cinebench.exe' | Select Status,StatusMessage | Format-List; Get-CimInstance Win32_ComputerSystem | Select TotalPhysicalMemory,NumberOfLogicalProcessors | Format-List"
exit /b %errorlevel%
