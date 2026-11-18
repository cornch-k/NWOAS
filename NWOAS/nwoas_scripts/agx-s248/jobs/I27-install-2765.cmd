@echo off
echo S248 I27 INSTALL 2765 (arm before install)
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v NwSoftPoolCached /t REG_DWORD /d 1 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v NwSoftOverlay /t REG_DWORD /d 0 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v G3Armed /t REG_DWORD /d 1 /f
cd /d C:\NWOAS\drop\agx-j274-2765
pnputil /add-driver AppleAgxRenderAdmission.arm64only.inf /install
echo S248 I27 PNPUTIL EXIT %ERRORLEVEL%
ping -n 21 127.0.0.1 >nul
powershell -NoProfile -Command "$d=Get-PnpDevice -InstanceId 'ACPI\APPL0002\0'; 'DEV1 '+$d.Status+' '+$d.Problem; if($d.Status -ne 'OK'){ reg add 'HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters' /v G3Armed /t REG_DWORD /d 1 /f | Out-Null; pnputil /restart-device 'ACPI\APPL0002\0' | Out-Null; Start-Sleep 20; $d=Get-PnpDevice -InstanceId 'ACPI\APPL0002\0'; 'DEV2 '+$d.Status+' '+$d.Problem }"
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v Wom1StartStage
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v Wom1StartStatus
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v NwScanoutPath
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v NwScanoutHwCaps
powershell -NoProfile -Command "Get-CimInstance Win32_VideoController | ForEach-Object { 'VC '+$_.Name+' | '+$_.DriverVersion+' | '+$_.Status }"
echo S248 I27 DONE
exit /b 0
