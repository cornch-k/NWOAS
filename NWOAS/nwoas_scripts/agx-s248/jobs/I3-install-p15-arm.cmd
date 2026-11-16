@echo off
echo S248 I3 INSTALL P15 + ARM
cd /d C:\NWOAS\drop\agx-j274-2744
pnputil /add-driver AppleAgxRenderAdmission.arm64only.inf /install
echo S248 I3 PNPUTIL EXIT %ERRORLEVEL%
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v G3Armed /t REG_DWORD /d 1 /f
pnputil /restart-device "ACPI\APPL0002\0"
echo S248 I3 RESTART EXIT %ERRORLEVEL%
ping -n 21 127.0.0.1 >nul
pnputil /enum-devices /instanceid "ACPI\APPL0002\0"
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" | findstr /i "Wom1StartStage Wom1StartStatus Wom1GpuvaArmPhase Wom1PostDisplay Wom1RtkitHello Wom1ProviderBootSucceeded Wom1BackendStartResult"
echo S248 I3 DONE
exit /b 0
