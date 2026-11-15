@echo off
rem S243 P1: proves PowerShell output returns through the worker job channel.
rem Win32_VideoController is EMPTY on this machine (no Display-class device; the
rem framebuffer is ROOT\BASICDISPLAY\0000 in class System), so resolution is read
rem from the display driver/CCD state instead. Read-only.
echo S243 P1 POWERSHELL DISPLAY
set PS="%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command
%PS% "'PS '+$PSVersionTable.PSVersion+' user '+[Security.Principal.WindowsIdentity]::GetCurrent().Name+' session '+(Get-Process -Id $PID).SessionId" 2>&1
%PS% "'Win32_VideoController count = '+@(Get-CimInstance Win32_VideoController).Count" 2>&1
%PS% "Get-CimInstance Win32_PnPEntity -Filter \"Service='BasicDisplay' OR Service='BasicRender' OR PNPClass='Monitor'\" | ForEach-Object { 'DEVICE '+$_.PNPClass+' | '+$_.Name+' | '+$_.Status+' | '+$_.DeviceID }" 2>&1
%PS% "Get-ChildItem 'HKLM:\SYSTEM\CurrentControlSet\Control\GraphicsDrivers\Configuration' | ForEach-Object { $m=Get-ItemProperty (Join-Path $_.PSPath '00\00') -ErrorAction SilentlyContinue; if($m){ 'CCD '+$_.PSChildName.Split('^')[0]+' active '+$m.'ActiveSize.cx'+'x'+$m.'ActiveSize.cy'+' primsurf '+$m.'PrimSurfSize.cx'+'x'+$m.'PrimSurfSize.cy'+' ts '+[DateTime]::FromFileTimeUtc((Get-ItemProperty $_.PSPath).Timestamp).ToString('u') } }" 2>&1
echo S243 P1 COMPLETE
exit /b 0
