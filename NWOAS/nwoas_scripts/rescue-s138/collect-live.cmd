@echo off
echo === S138 LIVE DIAGNOSTIC %date% %time% ===
echo --- BOOT ---
systeminfo | findstr /i /c:"System Boot Time" /c:"System Up Time" /c:"OS Name" /c:"OS Version" /c:"System Type" /c:"Processor(s)"
echo --- CPU ---
wmic cpu get Name,NumberOfCores,NumberOfLogicalProcessors,CurrentClockSpeed,MaxClockSpeed /format:list
echo --- PROBLEM DEVICES ---
pnputil /enum-devices /problem /deviceids /drivers
echo --- USB ---
pnputil /enum-devices /class USB /connected /deviceids /drivers
echo --- STORAGE ---
pnputil /enum-devices /class SCSIAdapter /connected /deviceids /drivers
echo --- RECENT BUGCHECKS ---
wevtutil qe System /q:"*[System[(EventID=41 or EventID=1001 or EventID=6008 or EventID=129 or EventID=153 or EventID=11 or EventID=15)]]" /rd:true /c:40 /f:text
echo --- DUMPS ---
dir /a /s C:\Windows\Minidump 2^>nul
dir /a C:\Windows\MEMORY.DMP 2^>nul
echo === COMPLETE ===
