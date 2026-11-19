@echo off
echo S258 W18 ETW WLAN STOP + CONVERT + FILTER (s258p copy, s258n masking: also 0x+12 hex and hex after Peer/self/DisAssoc)
if not exist "C:\NWOAS\drop\wifi-j274-20261119-0816-s258p\W18-etw-wlan-stop.ps1" (echo S258 W18 FAIL script missing & exit /b 1)
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "C:\NWOAS\drop\wifi-j274-20261119-0816-s258p\W18-etw-wlan-stop.ps1" 2>&1
echo S258 W18 DONE
exit /b 0
