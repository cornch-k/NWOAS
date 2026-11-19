@echo off
echo S273 R39 ROLLBACK 2777 -> 2776: check the 2776 files in C:\NWOAS\drop\agx-j274-2776, delete every appleagxrenderadmission.arm64x(wow).inf package with /uninstall, clear UserModeDriverNameWow, rename SysWOW64 Umd32 to .p42old, install 2776 (as I38)
powershell -NoProfile -Command "$d='C:\NWOAS\drop\agx-j274-2776'; $n=@('AppleAgxRenderAdmission.sys','AppleAgxRenderAdmissionUmd.dll','AppleAgxRenderAdmissionUmdArm64.dll','AppleAgxRenderAdmissionUmdX64.dll','AppleAgxRenderAdmission.arm64x.inf','appleagxrenderadmission.cat'); $h=@('7fcdbd7d787662992f02559fa149bf813db382633a338b66100f1b245ea9eaa8','8502d1f11eabdf218fe8da6cd443782792e57db8403123e6d5e19e64c09abc77','b6bd3eb9652758151a4124476d86fd2f9bb3b9aed8b79ac8d3be74d60b26ab8f','d0bf3c00a2358699f3d15040971ae2c5471778a00c915852cfaca097450fad74','6f9d076ce411978aada5f7853db52b04bfe05a20071f3bccfc0432be5e823fb0','e747c36ab8797f213da0ed11fa23aeed0f6af1a3b9fd049501b24c8d33863023'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $d $n[$i]; if(-not (Test-Path $f)){ 'R39 MISSING '+$n[$i]; $bad++ } elseif((Get-FileHash $f -Algorithm SHA256).Hash.ToLower() -ne $h[$i]){ 'R39 HASH MISMATCH '+$n[$i]; $bad++ } else { 'R39 OK '+$n[$i] } }; if($bad){ exit 1 } else { exit 0 }"
if errorlevel 1 (echo S273 R39 ABORT 2776 files not intact - nothing changed & exit /b 1)
powershell -NoProfile -Command "$o=(pnputil /enum-drivers) -join \"`n\"; foreach($b in ($o -split \"`n`n\")){ if($b -match 'appleagxrenderadmission\.arm64x(wow)?\.inf' -and $b -match '(oem\d+\.inf)'){ 'R39 DELETE '+$Matches[1]; pnputil /delete-driver $Matches[1] /uninstall /force } }"
reg delete "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v Wom1PoolCpuVisibleTopDown /f >nul 2>&1
powershell -NoProfile -Command "$k=(Get-PnpDeviceProperty -InstanceId 'ACPI\APPL0002\0' -KeyName DEVPKEY_Device_Driver -ErrorAction SilentlyContinue).Data; if($k){ Remove-ItemProperty -Path ('HKLM:\SYSTEM\CurrentControlSet\Control\Class\'+$k) -Name UserModeDriverNameWow -ErrorAction SilentlyContinue }; $f='C:\Windows\SysWOW64\AppleAgxRenderAdmissionUmd32.dll'; if(Test-Path $f){ Move-Item -LiteralPath $f -Destination ($f+'.p42old') -Force -ErrorAction SilentlyContinue; 'R39 SYSWOW64 renamed='+(Test-Path ($f+'.p42old')) } else { 'R39 SYSWOW64 none' }"
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v NwSoftPoolCached /t REG_DWORD /d 1 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v NwSoftOverlay /t REG_DWORD /d 0 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v G3Armed /t REG_DWORD /d 1 /f
cd /d C:\NWOAS\drop\agx-j274-2776
pnputil /add-driver AppleAgxRenderAdmission.arm64x.inf /install
echo S273 R39 PNPUTIL EXIT %ERRORLEVEL%
ping -n 21 127.0.0.1 >nul
powershell -NoProfile -Command "$d=Get-PnpDevice -InstanceId 'ACPI\APPL0002\0'; 'DEV1 '+$d.Status+' '+$d.Problem; Get-CimInstance Win32_VideoController | ForEach-Object { 'VC '+$_.Name+' | '+$_.DriverVersion+' | '+$_.Status }"
echo S273 R39 NOTE: VC must read 30.0.2776.0; restart Windows (W4) before judging - DWM keeps the old UMD mapped until then.
echo S273 R39 DONE
exit /b 0
