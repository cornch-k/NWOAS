@echo off
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; Write-Output ('UTC='+[DateTime]::UtcNow.ToString('o')); Get-TimeZone | Select Id,BaseUtcOffset | Format-List; Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control\TimeZoneInformation' | Select RealTimeIsUniversal,TimeZoneKeyName,ActiveTimeBias | Format-List"
exit /b %errorlevel%
