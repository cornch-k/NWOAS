@echo off
echo S269 R37 ROLLBACK 2775 -> 2774: check the 2774 files still in C:\NWOAS\drop\agx-j274-2774, delete every appleagxrenderadmission.arm64x.inf package (2775 and 2774 share the name) with /uninstall, install 2774 (as I36)
powershell -NoProfile -Command "$d='C:\NWOAS\drop\agx-j274-2774'; $n=@('AppleAgxRenderAdmission.sys','AppleAgxRenderAdmissionUmd.dll','AppleAgxRenderAdmissionUmdArm64.dll','AppleAgxRenderAdmissionUmdX64.dll','AppleAgxRenderAdmission.arm64x.inf','appleagxrenderadmission.cat'); $h=@('ef926ebf2226d0e63ca8dcd2a0d5f3a7ba43fc19ce471fceff79b95a709bcbe5','d8f5b7006c32aca88c97a14c8490f099f80f09913cb7a387b33547c8c2d6b923','0e076329fcaf39bf450caf608fdd71e03c5a9d6564d2dc58fc542acf740d6819','3bd02d9c7da3116bdf14d089c4138da1d21e76869bde16ae98edcf95ff58c43b','e884378040af056b28e5bf4341da02169776fa503ba757fdeb3c2db20aa3e9b5','813316feab6a0f1254c986b4c3646d38f02075c877fbd03032e372812a1e81d6'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $d $n[$i]; if(-not (Test-Path $f)){ 'R37 MISSING '+$n[$i]; $bad++ } elseif((Get-FileHash $f -Algorithm SHA256).Hash.ToLower() -ne $h[$i]){ 'R37 HASH MISMATCH '+$n[$i]; $bad++ } else { 'R37 OK '+$n[$i] } }; if($bad){ exit 1 } else { exit 0 }"
if errorlevel 1 (echo S269 R37 ABORT 2774 files not intact - nothing changed & exit /b 1)
powershell -NoProfile -Command "$o=(pnputil /enum-drivers) -join \"`n\"; foreach($b in ($o -split \"`n`n\")){ if($b -match 'appleagxrenderadmission\.arm64x\.inf' -and $b -match '(oem\d+\.inf)'){ 'R37 DELETE '+$Matches[1]; pnputil /delete-driver $Matches[1] /uninstall /force } }"
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v NwSoftPoolCached /t REG_DWORD /d 1 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v NwSoftOverlay /t REG_DWORD /d 0 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v G3Armed /t REG_DWORD /d 1 /f
cd /d C:\NWOAS\drop\agx-j274-2774
pnputil /add-driver AppleAgxRenderAdmission.arm64x.inf /install
echo S269 R37 PNPUTIL EXIT %ERRORLEVEL%
ping -n 21 127.0.0.1 >nul
powershell -NoProfile -Command "$d=Get-PnpDevice -InstanceId 'ACPI\APPL0002\0'; 'DEV1 '+$d.Status+' '+$d.Problem; Get-CimInstance Win32_VideoController | ForEach-Object { 'VC '+$_.Name+' | '+$_.DriverVersion+' | '+$_.Status }"
echo S269 R37 NOTE: VC must read 30.0.2774.0; restart Windows (W4) before judging - DWM keeps the old UMD mapped until then.
echo S269 R37 DONE
exit /b 0
