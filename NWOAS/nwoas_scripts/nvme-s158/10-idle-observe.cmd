@echo off
chcp 65001 >nul
echo === S158 bounded idle response check BEGIN ===
powershell -NoProfile -Command "1..6 | ForEach-Object { Write-Output ('S158_IDLE_SAMPLE '+$_+' '+(Get-Date -Format o)); Get-Process -Id $PID | Select-Object Id,CPU,WorkingSet64 | Format-List; if ($_ -lt 6) { Start-Sleep -Seconds 60 } }"
echo === S158 bounded idle response check END ===
exit /b 0
