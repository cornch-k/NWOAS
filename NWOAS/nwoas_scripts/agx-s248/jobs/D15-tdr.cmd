@echo off
echo S248 D15 TDR AND DWM
wevtutil qe System /c:20 /rd:true /f:text /q:"*[System[(EventID=4101 or EventID=4097 or EventID=4115 or EventID=14 or EventID=13) and TimeCreated[timediff(@SystemTime) <= 1200000]]]" | findstr /i /c:"Date" /c:"Event ID" /c:"Source" /c:"Description" /c:"AGX" /c:"timeout" /c:"0x"
wevtutil qe Application /c:10 /rd:true /f:text /q:"*[System[Provider[@Name='Desktop Window Manager' or @Name='Application Error'] and TimeCreated[timediff(@SystemTime) <= 1200000]]]" | findstr /i /c:"Date" /c:"Event ID" /c:"Source" /c:"dwm" /c:"0x"
powershell -NoProfile -Command "Get-Process dwm | Format-List Id,StartTime,CPU"
exit /b 0
