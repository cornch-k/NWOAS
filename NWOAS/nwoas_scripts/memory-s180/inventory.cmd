@echo off
powershell -NoProfile -Command "Get-CimInstance Win32_ComputerSystem | Select TotalPhysicalMemory,NumberOfLogicalProcessors | Format-List; Get-CimInstance Win32_OperatingSystem | Select TotalVisibleMemorySize,FreePhysicalMemory | Format-List; Get-CimInstance Win32_LogicalDisk | Select DeviceID,Size,FreeSpace | Format-Table"
exit /b %errorlevel%
