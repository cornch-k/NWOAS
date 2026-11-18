@echo off
echo S258 W1 INSTALL WIFI wifi-j274-20261118-1609-s258h (ACPI\NWOA4378, nwoa4378.sys WiFiCx)
cd /d C:\NWOAS\drop\wifi-j274-20261118-1609-s258h
if errorlevel 1 (echo S258 W1 FAIL no package dir & exit /b 1)
rem full bring-up (stages 1-5 passed with s258e); fast backplane mode; DMA buffers below 0xC0000000
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v StopAfter /t REG_DWORD /d 0 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v DmaMode /t REG_DWORD /d 0 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v LinkUpAtStart /t REG_DWORD /d 0 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v SettleMs /t REG_DWORD /d 0 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v AccessDelayUs /t REG_DWORD /d 0 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v PostWriteRead /t REG_DWORD /d 0 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v VerifyFirmware /t REG_DWORD /d 1 /f
pnputil /add-driver nwoa4378.inf /install
echo S258 W1 PNPUTIL EXIT %ERRORLEVEL%
ping -n 31 127.0.0.1 >nul
powershell -NoProfile -Command "$d=Get-PnpDevice -InstanceId 'ACPI\NWOA4378\0' -ErrorAction SilentlyContinue; if($d){ 'DEV '+$d.InstanceId+' status='+$d.Status+' problem='+$d.Problem+' class='+$d.Class+' name='+$d.FriendlyName; Get-PnpDeviceProperty -InstanceId 'ACPI\NWOA4378\0' -KeyName DEVPKEY_Device_DriverInfPath,DEVPKEY_Device_ProblemStatus -ErrorAction SilentlyContinue | ForEach-Object { 'PROP '+$_.KeyName+' = '+$_.Data } } else { 'DEV none' }"
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters"
netsh wlan show drivers
netsh wlan show interfaces
echo S258 W1 DONE
exit /b 0
