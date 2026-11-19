@echo off
echo S258 W17 ETW WLAN START (s258q copy; logman session NwoasWlan, circular 192 MB, C:\NWOAS\wifitrace\wlan.etl, no packet capture)
if not exist "C:\NWOAS\drop\wifi-j274-20261119-1020-s258q\W17-etw-wlan-start.ps1" (echo S258 W17 FAIL script missing, deliver s258q first & exit /b 1)
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "C:\NWOAS\drop\wifi-j274-20261119-1020-s258q\W17-etw-wlan-start.ps1" 2>&1
echo S258 W17 DONE
exit /b 0
