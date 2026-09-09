@echo off
echo === S138 MINIDUMP BEGIN ===
certutil -encode C:\Windows\Minidump\050722-5031-01.dmp C:\Windows\Temp\NWOAS-S138-DUMP.B64 >nul
if errorlevel 1 exit /b 21
type C:\Windows\Temp\NWOAS-S138-DUMP.B64
del /q C:\Windows\Temp\NWOAS-S138-DUMP.B64
echo === S138 MINIDUMP END ===
