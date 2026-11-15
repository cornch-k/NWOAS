@echo off
echo S231 J15 DELAYED WINPE REBOOT TO INTERNAL SSD WINDOWS
start "" /b cmd.exe /d /c "ping -n 8 127.0.0.1 >nul & wpeutil reboot"
echo REBOOT SCHEDULED IN ABOUT 7 SECONDS
exit /b 0
