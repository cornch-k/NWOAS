@echo off
echo === S146 LIVE STATE BEGIN %date% %time% ===
echo --- CPU ---
wmic cpu get Name,NumberOfCores,NumberOfLogicalProcessors,CurrentClockSpeed,MaxClockSpeed /format:list
echo --- OS ---
wmic os get Caption,Version,LastBootUpTime,FreePhysicalMemory /format:list
echo --- DISKS ---
wmic diskdrive get Index,Model,Size,Status
echo --- BOOT TIMER SETTINGS ---
bcdedit /enum {current}
echo --- RECENT WATCHDOG AND STORAGE EVENTS ---
wevtutil qe System /q:"*[System[(EventID=1001 or EventID=129 or EventID=153)]]" /rd:true /c:8 /f:text
echo === S146 LIVE STATE COMPLETE ===
exit /b 0
