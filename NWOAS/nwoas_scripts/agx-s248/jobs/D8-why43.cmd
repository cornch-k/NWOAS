@echo off
echo S248 D8 DEVPARAMS
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /s
echo S248 D8 SERVICE
reg query "HKLM\SYSTEM\CurrentControlSet\Services\AppleAgxRenderAdmission" /s
echo S248 D8 EVENTS
wevtutil qe System /c:25 /rd:true /f:text /q:"*[System[TimeCreated[timediff(@SystemTime) <= 1800000]]]" | findstr /i /c:"Provider" /c:"Event ID" /c:"Description" /c:"AGX" /c:"Display" /c:"dxg" /c:"APPL0002" /c:"0x"
exit /b 0
