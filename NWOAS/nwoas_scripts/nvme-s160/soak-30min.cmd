@echo off
chcp 65001 >nul
echo === S160 THIRTY MINUTE READ-MOSTLY SOAK BEGIN ===
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; 1..31 | ForEach-Object { Write-Output ('S160_SAMPLE '+$_+' '+(Get-Date -Format o)); & D:\CPUSTRES.EXE; if ($LASTEXITCODE -ne 0) { exit 11 }; & D:\DISKREAD.EXE; if ($LASTEXITCODE -ne 0) { exit 12 }; if ($_ -lt 31) { Start-Sleep -Seconds 60 } }"
if errorlevel 1 exit /b %errorlevel%
echo === S160 THIRTY MINUTE READ-MOSTLY SOAK PASS ===
exit /b 0
