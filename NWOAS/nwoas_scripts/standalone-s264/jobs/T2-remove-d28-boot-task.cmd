@echo off
echo S264C T2 REMOVE D28 BOOT TASK
schtasks /delete /f /tn "NWOAS-S264-D28"
echo S264C T2 SCHTASKS EXIT %ERRORLEVEL%
if exist C:\NWOAS\s264\d28-boot.log type C:\NWOAS\s264\d28-boot.log
echo S264C T2 DONE
exit /b 0
