@echo off
echo === S158 runtime counters BEGIN ===
powershell -NoProfile -Command "1..3 | ForEach-Object { Get-Date -Format o; Get-CimInstance Win32_PerfFormattedData_PerfOS_Processor | Where-Object Name -eq '_Total' | Select-Object Name,PercentProcessorTime,PercentDPCTime,PercentInterruptTime,InterruptsPersec | Format-List; Get-CimInstance Win32_PerfFormattedData_PerfDisk_PhysicalDisk | Select-Object Name,AvgDisksecPerRead,AvgDisksecPerWrite,CurrentDiskQueueLength,DiskBytesPersec | Format-Table -AutoSize; Start-Sleep -Seconds 5 }; Get-CimInstance Win32_OperatingSystem | Select-Object TotalVisibleMemorySize,FreePhysicalMemory,LastBootUpTime | Format-List"
echo === S158 runtime counters END ===
exit /b 0
