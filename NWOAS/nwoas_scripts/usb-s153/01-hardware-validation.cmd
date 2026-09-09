@echo off
echo === S153 HARDWARE VALIDATION BEGIN %date% %time% ===
echo --- CPU ---
wmic cpu get NumberOfCores,NumberOfLogicalProcessors,CurrentClockSpeed,MaxClockSpeed /format:list
D:\CPUSTRES.EXE
if errorlevel 1 exit /b %errorlevel%
echo --- DISK ---
D:\DISKREAD.EXE
if errorlevel 1 exit /b %errorlevel%
echo --- XHCI CONTROLLERS ---
wmic path Win32_PnPEntity where "PNPDeviceID='ACPI\\PNP0D10\\0' or PNPDeviceID='ACPI\\PNP0D15\\1'" get Name,PNPDeviceID,Status,ConfigManagerErrorCode /format:list
echo --- USB ROOT HUBS ---
wmic path Win32_PnPEntity where "PNPClass='USB' and Name='USB Root Hub (USB 3.0)'" get Name,PNPDeviceID,Status,ConfigManagerErrorCode /format:list
echo === S153 HARDWARE VALIDATION PASS ===
exit /b 0
