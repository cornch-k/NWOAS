@echo off
chcp 65001 >nul
echo === STORNVME SYS BEGIN ===
certutil -encode C:\Windows\System32\drivers\stornvme.sys C:\Windows\Temp\NWOAS-STORNVME.B64 >nul
if errorlevel 1 exit /b 21
type C:\Windows\Temp\NWOAS-STORNVME.B64
del /q C:\Windows\Temp\NWOAS-STORNVME.B64
echo === STORNVME SYS END ===
exit /b 0
