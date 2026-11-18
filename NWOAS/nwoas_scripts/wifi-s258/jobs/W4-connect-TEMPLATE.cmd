@echo off
rem S258 W4 (template): WPA2-PSK connect through the Windows supplicant.
rem Copy this file, replace BOTH placeholders below (network name and passphrase), queue the copy,
rem then delete the copy: the passphrase must not stay on disk.  This template is never queued as is.
set "NW_SSID=__SSID__"
set "NW_KEY=__PASSPHRASE__"
if "%NW_SSID%"=="__SSID__" (echo S258 W4 FAIL template not filled & exit /b 1)
echo S258 W4 CONNECT ssid=%NW_SSID%
powershell -NoProfile -Command "$s=[Security.SecurityElement]::Escape($env:NW_SSID); $k=[Security.SecurityElement]::Escape($env:NW_KEY); $x='<?xml version=\"1.0\"?><WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\"><name>'+$s+'</name><SSIDConfig><SSID><name>'+$s+'</name></SSID></SSIDConfig><connectionType>ESS</connectionType><connectionMode>manual</connectionMode><MSM><security><authEncryption><authentication>WPA2PSK</authentication><encryption>AES</encryption><useOneX>false</useOneX></authEncryption><sharedKey><keyType>passPhrase</keyType><protected>false</protected><keyMaterial>'+$k+'</keyMaterial></sharedKey></security></MSM></WLANProfile>'; Set-Content -Encoding UTF8 -Path $env:TEMP\nwoas-w4.xml -Value $x"
netsh wlan add profile filename="%TEMP%\nwoas-w4.xml" user=all
del /q "%TEMP%\nwoas-w4.xml"
set "NW_KEY="
netsh wlan connect name="%NW_SSID%"
ping -n 26 127.0.0.1 >nul
netsh wlan show interfaces
ipconfig
powershell -NoProfile -Command "$g=(Get-NetRoute -DestinationPrefix 0.0.0.0/0 -ErrorAction SilentlyContinue | Select-Object -First 1).NextHop; 'GATEWAY '+$g; if($g){ Test-Connection -Count 3 -ComputerName $g -ErrorAction SilentlyContinue | ForEach-Object { 'PING '+$_.Address+' '+$_.ResponseTime+' ms' } }"
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters"
echo S258 W4 DONE
exit /b 0
