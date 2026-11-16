@echo off
echo S248 D13 WOM1 AFTER D3D
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" > C:\NWOAS\d13.txt
findstr /v /r /c:"REG_BINARY    [0-9A-F]\{120\}" C:\NWOAS\d13.txt
findstr /i "SourceAddress DisplayModes Render Present Fault Soft Vsync" C:\NWOAS\d13.txt
exit /b 0
