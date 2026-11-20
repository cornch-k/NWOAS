@echo off
echo S274 R40 ROLLBACK 2778 -> 2777: check the 2777 files in C:\NWOAS\drop\agx-j274-2777, delete every appleagxrenderadmission.arm64x(wow).inf package with /uninstall, delete Wom1PoolCpuVisibleTopDown (2777: absent = off), install 2777 (as I39)
powershell -NoProfile -Command "$d='C:\NWOAS\drop\agx-j274-2777'; $n=@('AppleAgxRenderAdmission.sys','AppleAgxRenderAdmissionUmd.dll','AppleAgxRenderAdmissionUmdArm64.dll','AppleAgxRenderAdmissionUmdX64.dll','AppleAgxRenderAdmissionUmd32.dll','AppleAgxRenderAdmission.arm64xwow.inf','appleagxrenderadmission.cat'); $h=@('f256dbc4249f5665ffae7d1bfe2c34aa0b6da201f5d3c69878c17de91c60f8cf','6e7a375b2a738f1acbbca00edd7ee0a9b5156b189d01a38a71bc932bc90b404f','c3e2a3d851da501703d32fa0b2e20d741c1acb2a4c1e1e74d14aa0d12c6130ac','fc1475acddb21827280b0025657225eba624653bec755db55cebe6e448977178','5601111d01fdf721a3165668eb155667d4af9e1919e758641c273e53556b2a9a','2ea86858e37a3d6a86c129604e324a8d129e042cd6f161927e252ed27a74d193','bce86a72e4c9179c6d346d56d04cbba37f2807f7bcf892882c4360292fe844cf'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $d $n[$i]; if(-not (Test-Path $f)){ 'R40 MISSING '+$n[$i]; $bad++ } elseif((Get-FileHash $f -Algorithm SHA256).Hash.ToLower() -ne $h[$i]){ 'R40 HASH MISMATCH '+$n[$i]; $bad++ } else { 'R40 OK '+$n[$i] } }; if($bad){ exit 1 } else { exit 0 }"
if errorlevel 1 (echo S274 R40 ABORT 2777 files not intact - nothing changed & exit /b 1)
powershell -NoProfile -Command "$o=(pnputil /enum-drivers) -join \"`n\"; foreach($b in ($o -split \"`n`n\")){ if($b -match 'appleagxrenderadmission\.arm64x(wow)?\.inf' -and $b -match '(oem\d+\.inf)'){ 'R40 DELETE '+$Matches[1]; pnputil /delete-driver $Matches[1] /uninstall /force } }"
reg delete "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v Wom1PoolCpuVisibleTopDown /f >nul 2>&1
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v NwSoftPoolCached /t REG_DWORD /d 1 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v NwSoftOverlay /t REG_DWORD /d 0 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v G3Armed /t REG_DWORD /d 1 /f
cd /d C:\NWOAS\drop\agx-j274-2777
pnputil /add-driver AppleAgxRenderAdmission.arm64xwow.inf /install
echo S274 R40 PNPUTIL EXIT %ERRORLEVEL%
ping -n 21 127.0.0.1 >nul
powershell -NoProfile -Command "$d=Get-PnpDevice -InstanceId 'ACPI\APPL0002\0'; 'DEV1 '+$d.Status+' '+$d.Problem; Get-CimInstance Win32_VideoController | ForEach-Object { 'VC '+$_.Name+' | '+$_.DriverVersion+' | '+$_.Status }; $f='C:\Windows\SysWOW64\AppleAgxRenderAdmissionUmd32.dll'; if(Test-Path $f){ 'R40 SYSWOW64 Umd32 '+(Get-FileHash $f).Hash.ToLower() } else { 'R40 SYSWOW64 Umd32 MISSING' }"
echo S274 R40 NOTE: VC must read 30.0.2777.0; restart Windows (W4) before judging - DWM keeps the old UMD mapped until then.
echo S274 R40 DONE
exit /b 0
