@echo off
set "MEDIA="
for %%d in (C D E F G H I J) do if exist %%d:\sources\install.swm if exist %%d:\NWOAS-S122.RUNNING set "MEDIA=%%d:"
if not defined MEDIA exit /b 20
echo S125 USB BOOT FILES
dir /s /b %MEDIA%\EFI
if exist %MEDIA%\EFI\Microsoft\Boot\BCD bcdedit /store %MEDIA%\EFI\Microsoft\Boot\BCD /enum all
if exist %MEDIA%\Boot\BCD bcdedit /store %MEDIA%\Boot\BCD /enum all
echo S125 ACTIVE HAL EXTENSIONS
reg query HKLM\SYSTEM\CurrentControlSet\Control\HAL /s
echo S125 PE BOOT OPTIONS
reg query HKLM\SYSTEM\CurrentControlSet\Control /v SystemStartOptions
echo S125 INSPECTION FINISHED
exit /b 0
