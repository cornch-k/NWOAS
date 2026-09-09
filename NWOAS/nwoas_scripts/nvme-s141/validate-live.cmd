@echo off
echo === S141 VALIDATION BEGIN %date% %time% ===
wmic cpu get NumberOfCores,NumberOfLogicalProcessors,CurrentClockSpeed,MaxClockSpeed /format:list
echo --- CPU ---
D:\CPUSTRES.EXE
if errorlevel 1 exit /b %errorlevel%
echo --- DISK ---
D:\DISKREAD.EXE
if errorlevel 1 exit /b %errorlevel%
echo --- RECENT STORAGE AND BUGCHECK EVENTS ---
wevtutil qe System /q:"*[System[(EventID=1001 or EventID=129 or EventID=153)]]" /rd:true /c:5 /f:text
echo === S141 VALIDATION PASS ===
exit /b 0
