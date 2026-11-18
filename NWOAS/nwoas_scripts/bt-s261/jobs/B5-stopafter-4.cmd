@echo off
echo S261 B5 SET StopAfter=4 (0 = full bring-up + BthMini child) AND RESTART ACPI\NWOA5F69\0
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v StopAfter /t REG_DWORD /d 4 /f
pnputil /restart-device "ACPI\NWOA5F69\0"
echo S261 B5 RESTART EXIT %ERRORLEVEL%
ping -n 31 127.0.0.1 >nul
%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe -NoProfile -NonInteractive -Command "$ErrorActionPreference='SilentlyContinue'; $ds=@(Get-PnpDevice | Where-Object { $_.InstanceId -like 'ACPI\NWOA5F69\*' -or $_.InstanceId -like 'NWBT\BCM4378BTH\*' -or $_.InstanceId -like 'ACPI\NWOA4378\*' -or $_.Class -eq 'Bluetooth' }); if (-not ($ds | Where-Object { $_.InstanceId -like 'ACPI\NWOA5F69\*' })) { 'BT ABSENT (no ACPI\NWOA5F69: _STA=0 or not the S261 payload)' }; foreach ($d in $ds) { 'DEV '+$d.InstanceId+' status='+$d.Status+' problem='+$d.Problem+' present='+$d.Present+' class='+$d.Class+' name='+$d.FriendlyName }"
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters"
echo S261 B5 DONE
exit /b 0
