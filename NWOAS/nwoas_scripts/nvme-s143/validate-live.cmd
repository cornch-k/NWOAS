@echo off
echo === S143 VALIDATION BEGIN %date% %time% ===
echo --- BOOT TIMER SETTINGS ---
bcdedit /enum {current}
echo --- PROCESSOR IDLE SETTING ---
powercfg /query SCHEME_CURRENT SUB_PROCESSOR IDLEDISABLE
echo --- CPU INVENTORY ---
wmic cpu get NumberOfCores,NumberOfLogicalProcessors,CurrentClockSpeed,MaxClockSpeed /format:list
echo --- CPU NATIVE ---
D:\CPUSTRES.EXE
if errorlevel 1 exit /b %errorlevel%
echo --- DISK NATIVE 64 MIB ---
D:\DISKREAD.EXE
if errorlevel 1 exit /b %errorlevel%
echo --- 15 SECOND TIMER WAKE ---
ping 127.0.0.1 -n 16 >nul
echo === S143 VALIDATION PASS ===
exit /b 0
