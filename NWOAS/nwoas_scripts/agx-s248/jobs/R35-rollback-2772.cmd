@echo off
echo S248 R35 ROLLBACK 2773 -> 2772: check the 2772 files still in C:\NWOAS\drop\agx-j274-2772, delete the 2773 package (driver store) with /uninstall, install 2772 (as I34), report System32 leftovers
powershell -NoProfile -Command "$d='C:\NWOAS\drop\agx-j274-2772'; $n=@('AppleAgxRenderAdmission.sys','AppleAgxRenderAdmissionUmd.dll','AppleAgxRenderAdmission.arm64only.inf','appleagxrenderadmission.cat'); $h=@('cac0dabb81b712f5e375bb4ed22118244a529c58904858be01df3e1c04f059f5','8647b5df6627d2b9b2f795e043ed4a308576fb65d67d45836a4505c48b96b496','bf89c9c42503987842b79a8a30d2daca1c0dabf216e890b44c68f4817638e96d','15144633a9b74b79c62e79bb9a0a0577fb46c8042b0b86dd209a3d0d35f029d9'); $bad=0; for($i=0;$i -lt 4;$i++){ $f=Join-Path $d $n[$i]; if(!(Test-Path $f)){ 'R35 MISSING '+$n[$i]; $bad=1; continue }; $g=(Get-FileHash $f).Hash.ToLower(); if($g -ne $h[$i]){ 'R35 MISMATCH '+$n[$i]; $bad=1 } else { 'R35 OK '+$n[$i] } }; exit $bad"
if errorlevel 1 (echo S248 R35 ABORT 2772 files not intact - nothing changed & exit /b 1)
powershell -NoProfile -Command "$o=(pnputil /enum-drivers) -join \"`n\"; foreach($b in ($o -split \"`n`n\")){ if($b -match 'appleagxrenderadmission\.arm64x\.inf' -and $b -match '(oem\d+\.inf)'){ 'R35 DELETE '+$Matches[1]; pnputil /delete-driver $Matches[1] /uninstall /force } }"
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v NwSoftPoolCached /t REG_DWORD /d 1 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v NwSoftOverlay /t REG_DWORD /d 0 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v G3Armed /t REG_DWORD /d 1 /f
cd /d C:\NWOAS\drop\agx-j274-2772
pnputil /add-driver AppleAgxRenderAdmission.arm64only.inf /install
echo S248 R35 PNPUTIL EXIT %ERRORLEVEL%
ping -n 21 127.0.0.1 >nul
powershell -NoProfile -Command "$d=Get-PnpDevice -InstanceId 'ACPI\APPL0002\0'; 'DEV1 '+$d.Status+' '+$d.Problem; Get-CimInstance Win32_VideoController | ForEach-Object { 'VC '+$_.Name+' | '+$_.DriverVersion+' | '+$_.Status }; foreach($n in 'AppleAgxRenderAdmissionUmd.dll','AppleAgxRenderAdmissionUmdArm64.dll','AppleAgxRenderAdmissionUmdX64.dll'){ $f='C:\Windows\System32\'+$n; if(Test-Path $f){ 'SYS32 '+$n+' '+(Get-FileHash $f).Hash.ToLower() } else { 'SYS32 '+$n+' absent' } }"
echo S248 R35 NOTE: SYS32 AppleAgxRenderAdmissionUmd.dll must read 8647b5df...; UmdArm64/UmdX64 left over are not referenced by 2772 (DWM may still map UmdArm64 until the restart). Remove them after the restart with R35b.
echo S248 R35 DONE
exit /b 0
