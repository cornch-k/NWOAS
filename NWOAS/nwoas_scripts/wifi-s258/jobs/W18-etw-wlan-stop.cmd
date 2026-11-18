@echo off
echo S258 W18 ETW WLAN STOP + CONVERT + FILTER (disconnect/roam lines only, masked, about 30 KB)
if not exist "C:\NWOAS\drop\wifi-j274-20261119-0429-s258m\W18-etw-wlan-stop.ps1" (echo S258 W18 FAIL script missing & exit /b 1)
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "C:\NWOAS\drop\wifi-j274-20261119-0429-s258m\W18-etw-wlan-stop.ps1" 2>&1
echo S258 W18 DONE
exit /b 0
