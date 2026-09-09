@echo off
shutdown.exe /s /t 0 /d p:0:0 /c "NWOAS S141 storage experiment"
exit /b %errorlevel%
