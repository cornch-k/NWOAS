@echo off
chcp 65001 >nul
echo === STORPORT SYS BEGIN ===
certutil -encode C:\Windows\System32\drivers\storport.sys C:\Windows\Temp\NWOAS-STORPORT.B64 >nul
if errorlevel 1 exit /b 21
type C:\Windows\Temp\NWOAS-STORPORT.B64
del /q C:\Windows\Temp\NWOAS-STORPORT.B64
echo === STORPORT SYS END ===
exit /b 0
