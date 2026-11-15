@echo off
echo S231 W2 ALLOW OFFLINE OOBE (BypassNRO) AND RESTART
reg add HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\OOBE /v BypassNRO /t REG_DWORD /d 1 /f
reg query HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\OOBE /v BypassNRO
start "" /b cmd.exe /d /c "ping -n 8 127.0.0.1 >nul & shutdown /r /t 0 /f"
echo RESTART SCHEDULED
exit /b 0
