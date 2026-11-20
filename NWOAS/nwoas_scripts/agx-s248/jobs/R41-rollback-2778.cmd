@echo off
echo S279 R41 ROLLBACK 2779 -> 2778: check the 2778 files in C:\NWOAS\drop\agx-j274-2778, delete every appleagxrenderadmission.arm64x(wow).inf package with /uninstall, delete Wom1PoolCpuVisibleTopDown (2778: absent = on), install 2778 (as I41)
powershell -NoProfile -Command "$d='C:\NWOAS\drop\agx-j274-2778'; $n=@('AppleAgxRenderAdmission.sys','AppleAgxRenderAdmissionUmd.dll','AppleAgxRenderAdmissionUmdArm64.dll','AppleAgxRenderAdmissionUmdX64.dll','AppleAgxRenderAdmissionUmd32.dll','AppleAgxRenderAdmission.arm64xwow.inf','appleagxrenderadmission.cat'); $h=@('c06de374e1dfd20c05d8719b076ce197e5386abd81ef7444f789fc1bf7bc1cb0','77ae9e45fb5d06a5aed8ebe8e06f9a1d5e290db72f151ed09b085ae08261388d','e5e4f27b6c5e433a124803e3bdcf7eedc9ca1e878f3929f2b2674891f116629e','529d46c272c016a83cef5305820350023dc2e484ed03b2f56340d06517702c96','8c59885bf15c951601be4de77930a238e2352ca10ead1560e1ce28f0a2372fd4','e57fa00fcfe18fae9575db96bde8be74efba2aa56f72f68860209c245e18844d','edbb8708cd227f5b7748065fc3887101d54ef0434d37c5a930ae27946e4663de'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $d $n[$i]; if(-not (Test-Path $f)){ 'R41 MISSING '+$n[$i]; $bad++ } elseif((Get-FileHash $f -Algorithm SHA256).Hash.ToLower() -ne $h[$i]){ 'R41 HASH MISMATCH '+$n[$i]; $bad++ } else { 'R41 OK '+$n[$i] } }; if($bad){ exit 1 } else { exit 0 }"
if errorlevel 1 (echo S279 R41 ABORT 2778 files not intact - nothing changed & exit /b 1)
powershell -NoProfile -Command "$o=(pnputil /enum-drivers) -join \"`n\"; foreach($b in ($o -split \"`n`n\")){ if($b -match 'appleagxrenderadmission\.arm64x(wow)?\.inf' -and $b -match '(oem\d+\.inf)'){ 'R41 DELETE '+$Matches[1]; pnputil /delete-driver $Matches[1] /uninstall /force } }"
reg delete "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v Wom1PoolCpuVisibleTopDown /f >nul 2>&1
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v NwSoftPoolCached /t REG_DWORD /d 1 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v NwSoftOverlay /t REG_DWORD /d 0 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v G3Armed /t REG_DWORD /d 1 /f
cd /d C:\NWOAS\drop\agx-j274-2778
pnputil /add-driver AppleAgxRenderAdmission.arm64xwow.inf /install
echo S279 R41 PNPUTIL EXIT %ERRORLEVEL%
ping -n 21 127.0.0.1 >nul
powershell -NoProfile -Command "$d=Get-PnpDevice -InstanceId 'ACPI\APPL0002\0'; 'DEV1 '+$d.Status+' '+$d.Problem; Get-CimInstance Win32_VideoController | ForEach-Object { 'VC '+$_.Name+' | '+$_.DriverVersion+' | '+$_.Status }; $f='C:\Windows\SysWOW64\AppleAgxRenderAdmissionUmd32.dll'; if(Test-Path $f){ 'R41 SYSWOW64 Umd32 '+(Get-FileHash $f).Hash.ToLower() } else { 'R41 SYSWOW64 Umd32 MISSING' }"
echo S279 R41 NOTE: VC must read 30.0.2778.0; restart Windows (W4) before judging - DWM keeps the old UMD mapped until then.
echo S279 R41 DONE
exit /b 0
