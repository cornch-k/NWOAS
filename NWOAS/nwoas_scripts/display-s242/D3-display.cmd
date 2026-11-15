@echo off
echo S242 D3 DISPLAY STATE
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "Get-CimInstance Win32_VideoController | Format-List Name,Status,CurrentHorizontalResolution,CurrentVerticalResolution,CurrentRefreshRate,VideoModeDescription | Out-File -Encoding ascii C:\Windows\Temp\s242d3.txt" < NUL
echo PSRC=%ERRORLEVEL%
type C:\Windows\Temp\s242d3.txt
echo S242 D3 COMPLETE
exit /b 0
