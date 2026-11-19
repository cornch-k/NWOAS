@echo off
echo S266 R36 ROLLBACK 2774 -> 2773: check the 2773 files still in C:\NWOAS\drop\agx-j274-2773, delete every appleagxrenderadmission.arm64x.inf package (2774 and 2773 share the name) with /uninstall, install 2773 (as I35)
powershell -NoProfile -Command "$d='C:\NWOAS\drop\agx-j274-2773'; $n=@('AppleAgxRenderAdmission.sys','AppleAgxRenderAdmissionUmd.dll','AppleAgxRenderAdmissionUmdArm64.dll','AppleAgxRenderAdmissionUmdX64.dll','AppleAgxRenderAdmission.arm64x.inf','appleagxrenderadmission.cat'); $h=@('28cb665ecded4afc07f38f599c80af6d5410b9dc9697ae6b00c95e0006ea667a','6f10255356dd2956864f5bfdb8a59c25fe0501d3474459f6f66b04f9db562fbd','71e99344179800b6993691643fec1e88ef37de0d06ad8187a11934fad5132a58','712d067f0db4fc0684a9ff6508b94582286f746547d648b2c4c68ff633b1b064','56fd01224e4e60dea1d1a7bbc665c7f293c100886af90eb6b483e8fa10decbd1','6d137825f912ef65d1d5ba2efcd598c9503a9ee369df686faeedc4eaed525653'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $d $n[$i]; if(-not (Test-Path $f)){ 'R36 MISSING '+$n[$i]; $bad++ } elseif((Get-FileHash $f -Algorithm SHA256).Hash.ToLower() -ne $h[$i]){ 'R36 HASH MISMATCH '+$n[$i]; $bad++ } else { 'R36 OK '+$n[$i] } }; if($bad){ exit 1 } else { exit 0 }"
if errorlevel 1 (echo S266 R36 ABORT 2773 files not intact - nothing changed & exit /b 1)
powershell -NoProfile -Command "$o=(pnputil /enum-drivers) -join \"`n\"; foreach($b in ($o -split \"`n`n\")){ if($b -match 'appleagxrenderadmission\.arm64x\.inf' -and $b -match '(oem\d+\.inf)'){ 'R36 DELETE '+$Matches[1]; pnputil /delete-driver $Matches[1] /uninstall /force } }"
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v NwSoftPoolCached /t REG_DWORD /d 1 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v NwSoftOverlay /t REG_DWORD /d 0 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v G3Armed /t REG_DWORD /d 1 /f
cd /d C:\NWOAS\drop\agx-j274-2773
pnputil /add-driver AppleAgxRenderAdmission.arm64x.inf /install
echo S266 R36 PNPUTIL EXIT %ERRORLEVEL%
ping -n 21 127.0.0.1 >nul
powershell -NoProfile -Command "$d=Get-PnpDevice -InstanceId 'ACPI\APPL0002\0'; 'DEV1 '+$d.Status+' '+$d.Problem; Get-CimInstance Win32_VideoController | ForEach-Object { 'VC '+$_.Name+' | '+$_.DriverVersion+' | '+$_.Status }"
echo S266 R36 NOTE: VC must read 30.0.2773.0; restart Windows (W4) before judging - DWM keeps the old UMD mapped until then.
echo S266 R36 DONE
exit /b 0
