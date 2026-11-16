@echo off
echo S248 D11 WOM1 SMALL
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" > C:\NWOAS\d11.txt
findstr /v /r /c:"REG_BINARY    [0-9A-F]\{200\}" C:\NWOAS\d11.txt | findstr /i "Display Present Flip Vsync Refus Scanout Umd Wom1Start Mode Source Visible Dwm Render"
echo S248 D11 DXDIAG CARD
powershell -NoProfile -Command "$t=Get-Content C:\NWOAS\dxdiag.txt; $i=($t | Select-String -Pattern 'Card name: Apple').LineNumber; if($i){$t[($i-2)..($i+75)]}"
echo S248 D11 APP EVENTS
wevtutil qe Application /c:12 /rd:true /f:text /q:"*[System[TimeCreated[timediff(@SystemTime) <= 900000]]]" | findstr /i /c:"Source" /c:"Event ID" /c:"Description" /c:"dwm" /c:"Apple" /c:"dll" /c:"0x"
echo S248 D11 DISABLE AGX TO RESTORE BASIC DISPLAY
pnputil /disable-device "ACPI\APPL0002\0"
exit /b 0
