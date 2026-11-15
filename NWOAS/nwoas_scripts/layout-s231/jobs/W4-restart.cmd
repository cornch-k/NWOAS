@echo off
echo S231 W4 RESTART WINDOWS FOR S232 TEST
start "" /b cmd.exe /d /c "ping -n 8 127.0.0.1 >/dev/null & shutdown /r /t 0 /f"
echo RESTART SCHEDULED
exit /b 0
