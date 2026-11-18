@echo off
echo S256 A1 INSTALL AUDIO 20261118-1212-vol6 (ACPI\NWAU0001)
cd /d C:\NWOAS\drop\snd-j274-20261118-1212-vol6
if errorlevel 1 (echo S256 A1 FAIL no package dir & exit /b 1)
pnputil /add-driver NwoasAudio.inf /install
echo S256 A1 PNPUTIL EXIT %ERRORLEVEL%
ping -n 16 127.0.0.1 >nul
powershell -NoProfile -Command "$d=Get-PnpDevice -InstanceId 'ACPI\NWAU0001\0'; 'DEV '+$d.Status+' problem='+$d.Problem+' class='+$d.Class+' name='+$d.FriendlyName; $p=Get-PnpDeviceProperty -InstanceId 'ACPI\NWAU0001\0' -KeyName DEVPKEY_Device_DriverInfPath,DEVPKEY_Device_DriverVersion,DEVPKEY_Device_ProblemStatus -ErrorAction SilentlyContinue; $p | ForEach-Object { 'PROP '+$_.KeyName+' = '+$_.Data }"
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWAU0001\0\Device Parameters"
echo S256 A1 DONE
exit /b 0
