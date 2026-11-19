@echo off
echo GPUCOV K47t SET Wom1PoolCpuVisibleTopDown=1 for the NEXT KMD start (no device restart now: the 2776 mid-session restart left the device in CM_PROB_FAILED_POST_START)
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v Wom1PoolCpuVisibleTopDown /t REG_DWORD /d 1 /f
powershell -NoProfile -Command "'K47t TopDown='+(Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters').Wom1PoolCpuVisibleTopDown"
echo GPUCOV K47t DONE
exit /b 0
