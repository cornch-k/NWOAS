@echo off
echo S279 I42 INSTALL 2779 (P44: 2778 + DXBC translator mov-movc-swapc float modifiers, UMD only; the TopDown registry value is deleted so the default stays on; INF AppleAgxRenderAdmission.arm64xwow.inf)
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v NwSoftPoolCached /t REG_DWORD /d 1 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v NwSoftOverlay /t REG_DWORD /d 0 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v G3Armed /t REG_DWORD /d 1 /f
reg delete "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v Wom1PoolCpuVisibleTopDown /f >nul 2>&1
cd /d C:\NWOAS\drop\agx-j274-2779
pnputil /add-driver AppleAgxRenderAdmission.arm64xwow.inf /install
echo S279 I42 PNPUTIL EXIT %ERRORLEVEL%
ping -n 21 127.0.0.1 >nul
powershell -NoProfile -Command "$d=Get-PnpDevice -InstanceId 'ACPI\APPL0002\0'; 'DEV1 '+$d.Status+' '+$d.Problem; if($d.Status -ne 'OK'){ reg add 'HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters' /v G3Armed /t REG_DWORD /d 1 /f | Out-Null; pnputil /restart-device 'ACPI\APPL0002\0' | Out-Null; Start-Sleep 20; $d=Get-PnpDevice -InstanceId 'ACPI\APPL0002\0'; 'DEV2 '+$d.Status+' '+$d.Problem }"
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v Wom1StartStage
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v Wom1StartStatus
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v NwScanoutPath
powershell -NoProfile -Command "Get-CimInstance Win32_VideoController | ForEach-Object { 'VC '+$_.Name+' | '+$_.DriverVersion+' | '+$_.Status }"
powershell -NoProfile -Command "foreach($n in 'AppleAgxRenderAdmissionUmd.dll','AppleAgxRenderAdmissionUmdArm64.dll','AppleAgxRenderAdmissionUmdX64.dll'){ $f='C:\Windows\System32\'+$n; if(Test-Path $f){ $i=Get-Item $f; 'SYS32 '+$n+' '+$i.Length+' '+(Get-FileHash $f).Hash.ToLower()+' '+$i.VersionInfo.FileVersion } else { 'SYS32 '+$n+' MISSING' } }; $k=(Get-PnpDeviceProperty -InstanceId 'ACPI\APPL0002\0' -KeyName DEVPKEY_Device_Driver).Data; $r=Get-ItemProperty ('HKLM:\SYSTEM\CurrentControlSet\Control\Class\'+$k); 'UMDNAME '+(@($r.UserModeDriverName) -join ' | '); 'UMDNAMEX64 '+(@($r.UserModeDriverNameX64) -join ' | ')"
powershell -NoProfile -Command "foreach($n in 'vcruntime140.dll','vcruntime140_1.dll','msvcp140.dll'){ $f='C:\Windows\System32\'+$n; if(Test-Path $f){ 'CRT '+$n+' '+(Get-Item $f).VersionInfo.FileVersion } else { 'CRT '+$n+' MISSING (the x64 UMD imports it; install vc_redist.x64.exe, job I36 of S266)' } }; Get-ItemProperty HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\* -ErrorAction SilentlyContinue | Where-Object { $_.DisplayName -match 'Visual C\+\+' } | ForEach-Object { 'VCREDIST '+$_.DisplayName }"
powershell -NoProfile -Command "$f='C:\Windows\SysWOW64\AppleAgxRenderAdmissionUmd32.dll'; if(Test-Path $f){ $i=Get-Item $f; 'SYSWOW64 AppleAgxRenderAdmissionUmd32.dll '+$i.Length+' '+(Get-FileHash $f).Hash.ToLower()+' '+$i.VersionInfo.FileVersion } else { 'SYSWOW64 AppleAgxRenderAdmissionUmd32.dll MISSING' }; $k=(Get-PnpDeviceProperty -InstanceId 'ACPI\APPL0002\0' -KeyName DEVPKEY_Device_Driver).Data; $r=Get-ItemProperty ('HKLM:\SYSTEM\CurrentControlSet\Control\Class\'+$k); 'UMDNAMEWOW '+(@($r.UserModeDriverNameWow) -join ' | ')"
powershell -NoProfile -Command "foreach($n in 'vcruntime140.dll','msvcp140.dll'){ $f='C:\Windows\SysWOW64\'+$n; if(Test-Path $f){ 'CRT32 '+$n+' '+(Get-Item $f).VersionInfo.FileVersion } else { 'CRT32 '+$n+' MISSING (the x86 UMD imports it: install vc_redist.x86.exe)' } }"
powershell -NoProfile -Command "'I42 TOPDOWN_REG='+(Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters').Wom1PoolCpuVisibleTopDown+' (empty = absent = on since 2778)'"
echo S279 I42 DONE
exit /b 0
