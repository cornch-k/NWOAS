@echo off
echo S242 D1 DISPLAY STATE
powershell -NoProfile -Command "Get-CimInstance Win32_VideoController | Format-List Name,PNPDeviceID,Status,CurrentHorizontalResolution,CurrentVerticalResolution,CurrentRefreshRate,VideoModeDescription"
powershell -NoProfile -Command "Get-CimInstance -Namespace root\wmi WmiMonitorBasicDisplayParams -ErrorAction SilentlyContinue | Format-List InstanceName,MaxHorizontalImageSize,MaxVerticalImageSize"
echo S242 D1 COMPLETE
exit /b 0
