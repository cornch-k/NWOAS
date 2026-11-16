@echo off
echo S248 D10 ADAPTERS
powershell -NoProfile -Command "Get-CimInstance Win32_VideoController | Format-List Name,Status,CurrentHorizontalResolution,CurrentVerticalResolution,CurrentRefreshRate,DriverVersion,AdapterRAM"
echo S248 D10 DXDIAG
dxdiag /t C:\NWOAS\dxdiag.txt
ping -n 25 127.0.0.1 >nul
findstr /i /c:"Card name" /c:"Feature Levels" /c:"Driver Model" /c:"Display Memory" /c:"Dedicated Memory" /c:"Shared Memory" /c:"Current Mode" /c:"DDI Version" /c:"Driver Version" /c:"Hardware Scheduling" C:\NWOAS\dxdiag.txt
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" | findstr /i "Present Flip Vsync Scanout"
exit /b 0
