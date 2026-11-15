@echo off
echo S236 B0 RECORD BCD STATE
bcdedit /enum {current}
bcdedit /enum {badmemory}
systeminfo > C:\NWT\si.txt
findstr /c:"Total Physical" C:\NWT\si.txt
echo S236 B0 COMPLETE
exit /b 0
