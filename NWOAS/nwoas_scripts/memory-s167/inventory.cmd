@echo off
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; $os=Get-CimInstance Win32_OperatingSystem; $cs=Get-CimInstance Win32_ComputerSystem; $p=Get-CimInstance Win32_Processor; [pscustomobject]@{TotalVisibleKB=$os.TotalVisibleMemorySize;FreePhysicalKB=$os.FreePhysicalMemory;TotalPhysicalBytes=$cs.TotalPhysicalMemory;Cores=$p.NumberOfCores;Logical=$p.NumberOfLogicalProcessors;Boot=$os.LastBootUpTime} | Format-List; Get-CimInstance Win32_LogicalDisk | Select DeviceID,VolumeName,Size,FreeSpace | Format-Table -AutoSize"
exit /b %errorlevel%
