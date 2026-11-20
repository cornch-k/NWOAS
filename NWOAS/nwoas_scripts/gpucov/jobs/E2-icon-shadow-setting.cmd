@echo off
rem Read-only: desktop icon label drop shadow setting for every loaded user hive, plus visual effects mode.
echo E2 BEGIN
powershell -NoProfile -Command "Get-ChildItem Registry::HKEY_USERS | Where-Object { $_.Name -match 'S-1-5-21-[0-9-]+$' } | ForEach-Object { $k=$_.Name; $a=Get-ItemProperty -Path ('Registry::'+$k+'\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced') -ErrorAction SilentlyContinue; $v=Get-ItemProperty -Path ('Registry::'+$k+'\Software\Microsoft\Windows\CurrentVersion\Explorer\VisualEffects') -ErrorAction SilentlyContinue; $d=Get-ItemProperty -Path ('Registry::'+$k+'\Control Panel\Desktop') -ErrorAction SilentlyContinue; 'E2 USER ' + $k.Split('\')[-1] + ' ListviewShadow=' + $a.ListviewShadow + ' VisualFXSetting=' + $v.VisualFXSetting + ' UserPreferencesMask=' + (($d.UserPreferencesMask | ForEach-Object { $_.ToString('x2') }) -join '') }"
powershell -NoProfile -Command "$p=Get-ItemProperty 'HKLM:\SOFTWARE\NWOAS\AgxD3d' -ErrorAction SilentlyContinue; 'E2 PIXELCENTER=' + $p.D3d11PixelCenter; (Get-CimInstance Win32_VideoController | ForEach-Object { 'E2 GPU ' + $_.Name + ' ' + $_.DriverVersion + ' ' + $_.Status })"
echo E2 DONE
exit /b 0
