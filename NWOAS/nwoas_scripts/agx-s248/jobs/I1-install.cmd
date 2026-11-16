@echo off
echo S248 I1 INSTALL J274 AGX
cd /d C:\NWOAS\drop\agx-j274-2742
pnputil /add-driver AppleAgxRenderAdmission.arm64only.inf /install
echo S248 I1 PNPUTIL EXIT %ERRORLEVEL%
ping -n 6 127.0.0.1 >nul
pnputil /enum-devices /instanceid "ACPI\APPL0002\0" /drivers
sc query type= driver | findstr /i "agx"
echo S248 I1 DONE
exit /b 0
