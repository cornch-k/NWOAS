@echo off
echo S248 I4 VCREDIST + ENABLE AGX
C:\NWOAS\drop\vcredist\vc_redist.arm64.exe /install /quiet /norestart /log C:\NWOAS\vcredist.log
echo S248 I4 VCREDIST EXIT %ERRORLEVEL%
for %%f in (MSVCP140.dll VCRUNTIME140.dll) do (if exist C:\Windows\System32\%%f (echo FOUND %%f) else (echo MISSING %%f))
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v G3Armed /t REG_DWORD /d 1 /f
pnputil /enable-device "ACPI\APPL0002\0"
echo S248 I4 ENABLE EXIT %ERRORLEVEL%
ping -n 21 127.0.0.1 >nul
powershell -NoProfile -Command "Get-PnpDevice -InstanceId 'ACPI\APPL0002\0' | Format-List Status,Problem,FriendlyName"
dxdiag /t C:\NWOAS\dxdiag2.txt
ping -n 25 127.0.0.1 >nul
findstr /i /c:"Card name" /c:"Feature Levels" /c:"DDI Version" /c:"D3D Status" /c:"Current Mode" C:\NWOAS\dxdiag2.txt
exit /b 0
