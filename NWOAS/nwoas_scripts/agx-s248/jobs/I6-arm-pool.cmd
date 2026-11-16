@echo off
echo S248 I6 ARM + RESTART WITH POOL
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v G3Armed /t REG_DWORD /d 1 /f
pnputil /restart-device "ACPI\APPL0002\0"
ping -n 41 127.0.0.1 >nul
powershell -NoProfile -Command "Get-PnpDevice -InstanceId 'ACPI\APPL0002\0' | Format-List Status,Problem; Get-Process dwm | Format-List Id,StartTime"
wevtutil qe Application /c:5 /rd:true /f:text /q:"*[System[Provider[@Name='Application Error'] and TimeCreated[timediff(@SystemTime) <= 120000]]]" | findstr /i /c:"Date" /c:"dwm" /c:"0x"
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v Wom1M2PoolProbe
exit /b 0
