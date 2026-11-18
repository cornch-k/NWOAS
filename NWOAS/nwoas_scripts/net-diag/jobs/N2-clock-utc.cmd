@echo off
echo NWOAS N2 CLOCK: hardware clock holds UTC (as macOS keeps it) - tell Windows, then sync
reg query "HKLM\SYSTEM\CurrentControlSet\Control\TimeZoneInformation" /v RealTimeIsUniversal 2>&1
reg add "HKLM\SYSTEM\CurrentControlSet\Control\TimeZoneInformation" /v RealTimeIsUniversal /t REG_DWORD /d 1 /f
sc qc w32time | findstr /i "START_TYPE"
sc config w32time start= auto
net start w32time 2>&1
w32tm /config /manualpeerlist:time.windows.com,0x9 /syncfromflags:manual /update 2>&1
w32tm /resync /force 2>&1
powershell -NoProfile -Command "'UTC_NOW '+(Get-Date).ToUniversalTime().ToString('u')+' LOCAL '+(Get-Date).ToString('u')"
echo NWOAS N2 DONE
