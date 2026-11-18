@echo off
echo S248 D60 D3D10/11 MSAA OFF (the 2769 answers) for every process: HKLM\SOFTWARE\NWOAS\AgxD3d D3d11Msaa=0, then a GPU device restart so DWM and every app recreate their devices
reg add "HKLM\SOFTWARE\NWOAS\AgxD3d" /v D3d11Msaa /t REG_DWORD /d 0 /f /reg:64
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v G3Armed /t REG_DWORD /d 1 /f >nul
pnputil /restart-device "ACPI\APPL0002\0"
ping -n 21 127.0.0.1 >nul
powershell -NoProfile -Command "$d=Get-PnpDevice -InstanceId 'ACPI\APPL0002\0'; 'DEV '+$d.Status+' '+$d.Problem; $p=Get-Process dwm -ErrorAction SilentlyContinue; 'DWM pids='+(($p | ForEach-Object { $_.Id }) -join ',')"
echo S248 D60 DONE
exit /b 0
