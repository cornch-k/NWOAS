@echo off
echo S232 T2 RECORD MEMORY, REVERT TRUNCATEMEMORY, RESTART
systeminfo > C:\NWT\si.txt
findstr /c:"MB" C:\NWT\si.txt
bcdedit /deletevalue {current} truncatememory
bcdedit /enum {current}
start "" /b cmd.exe /d /c "ping -n 8 127.0.0.1 >nul & shutdown /r /t 0 /f"
echo RESTART SCHEDULED
exit /b 0
