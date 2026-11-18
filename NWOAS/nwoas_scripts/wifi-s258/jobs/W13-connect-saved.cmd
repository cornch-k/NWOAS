@echo off
echo S258 W13 CONNECT SAVED PROFILE home-ap (no password handled here)
netsh wlan show profiles
netsh wlan connect name=home-ap
ping -n 16 127.0.0.1 >nul
netsh wlan show interfaces
echo S258 W13 DONE
exit /b 0
