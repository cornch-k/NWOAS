@echo off
echo S123 controlled reboot to verify USB autostart
wpeutil reboot
exit /b %errorlevel%
