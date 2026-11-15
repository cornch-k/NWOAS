@echo off
echo S232 T1 TRUNCATE WINDOWS MEMORY TO 4 GIB (DECISION D4, REVERSIBLE) AND RESTART
bcdedit /set {current} truncatememory 0x100000000
bcdedit /enum {current}
start "" /b cmd.exe /d /c "ping -n 8 127.0.0.1 >nul & shutdown /r /t 0 /f"
echo RESTART SCHEDULED
exit /b 0
