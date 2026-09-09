@echo off
chcp 65001 >nul
echo === NWOAS RUNTIME LOAD SAMPLING ===
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; 1..10 | ForEach-Object { Get-CimInstance Win32_PerfFormattedData_PerfOS_Processor -Filter 'Name=''_Total''' | Select-Object Name,PercentProcessorTime,PercentDPCTime,PercentInterruptTime,InterruptsPersec | Format-List; Start-Sleep -Seconds 2 }"
if errorlevel 1 exit /b 1
echo === NWOAS RUNTIME LOAD COMPLETE ===
exit /b 0
