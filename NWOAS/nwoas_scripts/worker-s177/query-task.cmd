@echo off
schtasks.exe /Query /TN "NWOAS Agent" /XML
exit /b %errorlevel%
