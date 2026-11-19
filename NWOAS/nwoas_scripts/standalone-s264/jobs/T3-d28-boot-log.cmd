@echo off
echo S264C T3 D28 BOOT TASK LOG (read only)
schtasks /query /tn "NWOAS-S264-D28" /fo LIST /v
if exist C:\NWOAS\s264\d28-boot.log (type C:\NWOAS\s264\d28-boot.log) else (echo S264C T3 no log yet)
echo S264C T3 DONE
exit /b 0
