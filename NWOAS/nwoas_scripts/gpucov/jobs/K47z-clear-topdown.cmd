@echo off
echo GPUCOV K47z CLEAR Wom1PoolCpuVisibleTopDown (back to 2775 placement at the next KMD start; no device restart now)
reg delete "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v Wom1PoolCpuVisibleTopDown /f
powershell -NoProfile -Command "'K47z TopDown='+(Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters').Wom1PoolCpuVisibleTopDown"
echo GPUCOV K47z DONE
exit /b 0
