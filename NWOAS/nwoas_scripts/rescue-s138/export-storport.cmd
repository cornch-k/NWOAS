@echo off
echo === STORPORT BEGIN ===
certutil -encode C:\Windows\System32\drivers\storport.sys C:\Windows\Temp\NWOAS-STORPORT.B64 >nul
if errorlevel 1 exit /b 23
type C:\Windows\Temp\NWOAS-STORPORT.B64
del /q C:\Windows\Temp\NWOAS-STORPORT.B64
echo === STORPORT END ===
