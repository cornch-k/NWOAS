@echo off
echo S242 D2 DISPLAY STATE
where powershell 2>&1
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "Get-CimInstance Win32_VideoController | Format-List Name,Status,CurrentHorizontalResolution,CurrentVerticalResolution,CurrentRefreshRate,VideoModeDescription" 2>&1
echo PSRC=%ERRORLEVEL%
echo S242 D2 COMPLETE
exit /b 0
