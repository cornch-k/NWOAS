@echo off
echo S248 I2 INSTALL P14 + ARM
cd /d C:\NWOAS\drop\agx-j274-2743
pnputil /add-driver AppleAgxRenderAdmission.arm64only.inf /install
echo S248 I2 PNPUTIL EXIT %ERRORLEVEL%
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v G3Armed /t REG_DWORD /d 1 /f
pnputil /restart-device "ACPI\APPL0002\0"
echo S248 I2 RESTART EXIT %ERRORLEVEL%
ping -n 16 127.0.0.1 >nul
pnputil /enum-devices /instanceid "ACPI\APPL0002\0"
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" | findstr /i "Wom1Start Wom1GpuvaArm G3Armed Wom1Fw Wom1Firmware Wom1Power"
echo S248 I2 DONE
exit /b 0
