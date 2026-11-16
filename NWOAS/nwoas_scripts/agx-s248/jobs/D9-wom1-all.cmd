@echo off
echo S248 D9 ALL WOM1
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" > C:\NWOAS\d9.txt
findstr /v /r /c:"REG_BINARY    [0-9A-F]\{300\}" C:\NWOAS\d9.txt
echo S248 D9 KERNEL EVENTS
wevtutil qe System /c:15 /rd:true /f:text /q:"*[System[Provider[@Name='Microsoft-Windows-Kernel-PnP' or @Name='Display' or @Name='Microsoft-Windows-DxgKrnl']]]" | findstr /i /c:"Date" /c:"Event ID" /c:"APPL0002" /c:"0x" /c:"Description"
exit /b 0
