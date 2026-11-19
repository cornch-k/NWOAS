@echo off
echo S272 R38 ROLLBACK 2776 -> 2775: check the 2775 files still in C:\NWOAS\drop\agx-j274-2775, clear Wom1PoolCpuVisibleTopDown, delete every appleagxrenderadmission.arm64x.inf package with /uninstall, install 2775 (as I37)
powershell -NoProfile -Command "$d='C:\NWOAS\drop\agx-j274-2775'; $n=@('AppleAgxRenderAdmission.sys','AppleAgxRenderAdmissionUmd.dll','AppleAgxRenderAdmissionUmdArm64.dll','AppleAgxRenderAdmissionUmdX64.dll','AppleAgxRenderAdmission.arm64x.inf','appleagxrenderadmission.cat'); $h=@('2c451f39a30c7d98c4b309bd130ad8f7cb397d15834ab7b57eaa5636685128e3','9b3d5876b97ceeb5d873dc67231305aee7db2de52bbaa37cd07ee3125e5ec1de','4210c890b80144f549f5535134fe896873e323a9782871216f9ab234f5351687','825cf0f079683359a6d9dfcd716eb7280667d7ac7c47361d76bf9d84118d9fd2','2352da497e6d7c0aad893b54588edafa4ba8996d3df8e5e26ee7ef2e2edda4ca','a261e41ba6c2e3c0bc158f9edc1a6e09839c9d8b13fafc7186f561a0cdcdcbfb'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $d $n[$i]; if(-not (Test-Path $f)){ 'R38 MISSING '+$n[$i]; $bad++ } elseif((Get-FileHash $f -Algorithm SHA256).Hash.ToLower() -ne $h[$i]){ 'R38 HASH MISMATCH '+$n[$i]; $bad++ } else { 'R38 OK '+$n[$i] } }; if($bad){ exit 1 } else { exit 0 }"
if errorlevel 1 (echo S272 R38 ABORT 2775 files not intact - nothing changed & exit /b 1)
powershell -NoProfile -Command "$o=(pnputil /enum-drivers) -join \"`n\"; foreach($b in ($o -split \"`n`n\")){ if($b -match 'appleagxrenderadmission\.arm64x\.inf' -and $b -match '(oem\d+\.inf)'){ 'R38 DELETE '+$Matches[1]; pnputil /delete-driver $Matches[1] /uninstall /force } }"
reg delete "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v Wom1PoolCpuVisibleTopDown /f >nul 2>&1
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v NwSoftPoolCached /t REG_DWORD /d 1 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v NwSoftOverlay /t REG_DWORD /d 0 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v G3Armed /t REG_DWORD /d 1 /f
cd /d C:\NWOAS\drop\agx-j274-2775
pnputil /add-driver AppleAgxRenderAdmission.arm64x.inf /install
echo S272 R38 PNPUTIL EXIT %ERRORLEVEL%
ping -n 21 127.0.0.1 >nul
powershell -NoProfile -Command "$d=Get-PnpDevice -InstanceId 'ACPI\APPL0002\0'; 'DEV1 '+$d.Status+' '+$d.Problem; Get-CimInstance Win32_VideoController | ForEach-Object { 'VC '+$_.Name+' | '+$_.DriverVersion+' | '+$_.Status }"
echo S272 R38 NOTE: VC must read 30.0.2775.0; restart Windows (W4) before judging - DWM keeps the old UMD mapped until then.
echo S272 R38 DONE
exit /b 0
