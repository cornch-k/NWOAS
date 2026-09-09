@echo off
echo === S153 ROOT HUB CODE43 BEGIN ===
echo --- DEVICE AND DRIVER ---
pnputil /enum-devices /instanceid "USB\ROOT_HUB30\3&23492A9B&0&0" /drivers /properties
echo --- KERNEL PNP EVENTS ---
wevtutil qe System /q:"*[System[Provider[@Name='Microsoft-Windows-Kernel-PnP'] and (EventID=219 or EventID=225 or EventID=411 or EventID=442)]]" /rd:true /c:12 /f:text
echo --- USBXHCI EVENTS ---
wevtutil qe System /q:"*[System[Provider[@Name='Microsoft-Windows-USB-USBXHCI']]]" /rd:true /c:12 /f:text
echo === S153 ROOT HUB CODE43 END ===
exit /b 0
