@echo off
echo S248 I7 ARM + TRACE
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v G3Armed /t REG_DWORD /d 1 /f
pnputil /restart-device "ACPI\APPL0002\0"
ping -n 31 127.0.0.1 >nul
if exist C:\NWOAS\umd-trace.log (for %%A in (C:\NWOAS\umd-trace.log) do echo TRACE BYTES %%~zA) else (echo NO TRACE FILE)
powershell -NoProfile -Command "if(Test-Path C:\NWOAS\umd-trace.log){Get-Content C:\NWOAS\umd-trace.log -TotalCount 60}"
pnputil /disable-device "ACPI\APPL0002\0"
exit /b 0
