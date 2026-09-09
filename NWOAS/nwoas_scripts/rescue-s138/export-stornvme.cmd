@echo off
echo === STORNVME BEGIN ===
certutil -encode C:\Windows\System32\drivers\stornvme.sys C:\Windows\Temp\NWOAS-STORNVME.B64 >nul
if errorlevel 1 exit /b 22
type C:\Windows\Temp\NWOAS-STORNVME.B64
del /q C:\Windows\Temp\NWOAS-STORNVME.B64
echo === STORNVME END ===
