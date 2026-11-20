@echo off
echo S280 R42 ROLLBACK 2780 -> 2779: check the 2779 files in C:\NWOAS\drop\agx-j274-2779, delete every appleagxrenderadmission.arm64x(wow).inf package with /uninstall, delete Wom1PoolCpuVisibleTopDown (absent = on), install 2779 (as I42)
powershell -NoProfile -Command "$d='C:\NWOAS\drop\agx-j274-2779'; $n=@('AppleAgxRenderAdmission.sys','AppleAgxRenderAdmissionUmd.dll','AppleAgxRenderAdmissionUmdArm64.dll','AppleAgxRenderAdmissionUmdX64.dll','AppleAgxRenderAdmissionUmd32.dll','AppleAgxRenderAdmission.arm64xwow.inf','appleagxrenderadmission.cat'); $h=@('472b97c03e04ac127b1dac6e595240def6956efa3adc451783a08a1c7bbc7299','f7477213290b9827f00091e3a595c3e94937f35b378eaba19ae28b1ddb3d6140','c0122e1780ad83607ade4713d75bf51b65101e2d5ff9d0db3d6d269621806330','c2352c6cf6f53ebc3711f3d724eb8e544d6da27e372a1c1912da1193f6c951e7','0be8462d2156ec52a728f7a4f96e627caca68d6d252ac2d72f85981a4e829221','eb55bdeb761e86c20b8252328e21ad29738a65a18a1c00ac8e6162b6d58d304f','3b316ba49a2bb7474893a569d1d6cbc8e726e623f4a939935f2eb75800adde8e'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $d $n[$i]; if(-not (Test-Path $f)){ 'R42 MISSING '+$n[$i]; $bad++ } elseif((Get-FileHash $f -Algorithm SHA256).Hash.ToLower() -ne $h[$i]){ 'R42 HASH MISMATCH '+$n[$i]; $bad++ } else { 'R42 OK '+$n[$i] } }; if($bad){ exit 1 } else { exit 0 }"
if errorlevel 1 (echo S280 R42 ABORT 2779 files not intact - nothing changed & exit /b 1)
powershell -NoProfile -Command "$o=(pnputil /enum-drivers) -join \"`n\"; foreach($b in ($o -split \"`n`n\")){ if($b -match 'appleagxrenderadmission\.arm64x(wow)?\.inf' -and $b -match '(oem\d+\.inf)'){ 'R42 DELETE '+$Matches[1]; pnputil /delete-driver $Matches[1] /uninstall /force } }"
reg delete "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v Wom1PoolCpuVisibleTopDown /f >nul 2>&1
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v NwSoftPoolCached /t REG_DWORD /d 1 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v NwSoftOverlay /t REG_DWORD /d 0 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v G3Armed /t REG_DWORD /d 1 /f
cd /d C:\NWOAS\drop\agx-j274-2779
pnputil /add-driver AppleAgxRenderAdmission.arm64xwow.inf /install
echo S280 R42 PNPUTIL EXIT %ERRORLEVEL%
ping -n 21 127.0.0.1 >nul
powershell -NoProfile -Command "$d=Get-PnpDevice -InstanceId 'ACPI\APPL0002\0'; 'DEV1 '+$d.Status+' '+$d.Problem; Get-CimInstance Win32_VideoController | ForEach-Object { 'VC '+$_.Name+' | '+$_.DriverVersion+' | '+$_.Status }; $f='C:\Windows\SysWOW64\AppleAgxRenderAdmissionUmd32.dll'; if(Test-Path $f){ 'R42 SYSWOW64 Umd32 '+(Get-FileHash $f).Hash.ToLower() } else { 'R42 SYSWOW64 Umd32 MISSING' }"
echo S280 R42 NOTE: VC must read 30.0.2779.0; restart Windows (W4) before judging - DWM keeps the old UMD mapped until then.
echo S280 R42 DONE
exit /b 0
