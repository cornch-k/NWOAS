@echo off
echo S243 DELIVER BEGIN 7 file(s) to C:\NWOAS\drop\agx-j274-2779
set "T=C:\NWOAS\drop\agx-j274-2779"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $p=$env:T; while($p){ if(Test-Path -LiteralPath $p){ $x=Get-Item -LiteralPath $p -Force; if(-not $x.PSIsContainer -or ($x.Attributes -band [IO.FileAttributes]::ReparsePoint)){ throw ('unsafe path component '+$p) } }; $p=Split-Path -Parent $p }; New-Item -ItemType Directory -Force -Path $env:T | Out-Null; 'S243 DIR READY '+$env:T" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL dir & exit /b 1)
"%~d0\NWGET243.EXE" 0 472b97c03e04ac127b1dac6e595240def6956efa3adc451783a08a1c7bbc7299 "%T%\AppleAgxRenderAdmission.sys" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 0 & exit /b 1)
"%~d0\NWGET243.EXE" 1 f7477213290b9827f00091e3a595c3e94937f35b378eaba19ae28b1ddb3d6140 "%T%\AppleAgxRenderAdmissionUmd.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 1 & exit /b 1)
"%~d0\NWGET243.EXE" 2 c0122e1780ad83607ade4713d75bf51b65101e2d5ff9d0db3d6d269621806330 "%T%\AppleAgxRenderAdmissionUmdArm64.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 2 & exit /b 1)
"%~d0\NWGET243.EXE" 3 c2352c6cf6f53ebc3711f3d724eb8e544d6da27e372a1c1912da1193f6c951e7 "%T%\AppleAgxRenderAdmissionUmdX64.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 3 & exit /b 1)
"%~d0\NWGET243.EXE" 4 0be8462d2156ec52a728f7a4f96e627caca68d6d252ac2d72f85981a4e829221 "%T%\AppleAgxRenderAdmissionUmd32.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 4 & exit /b 1)
"%~d0\NWGET243.EXE" 5 eb55bdeb761e86c20b8252328e21ad29738a65a18a1c00ac8e6162b6d58d304f "%T%\AppleAgxRenderAdmission.arm64xwow.inf" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 5 & exit /b 1)
"%~d0\NWGET243.EXE" 6 3b316ba49a2bb7474893a569d1d6cbc8e726e623f4a939935f2eb75800adde8e "%T%\appleagxrenderadmission.cat" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 6 & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('AppleAgxRenderAdmission.sys','AppleAgxRenderAdmissionUmd.dll','AppleAgxRenderAdmissionUmdArm64.dll','AppleAgxRenderAdmissionUmdX64.dll','AppleAgxRenderAdmissionUmd32.dll','AppleAgxRenderAdmission.arm64xwow.inf','appleagxrenderadmission.cat'); $h=@('472b97c03e04ac127b1dac6e595240def6956efa3adc451783a08a1c7bbc7299','f7477213290b9827f00091e3a595c3e94937f35b378eaba19ae28b1ddb3d6140','c0122e1780ad83607ade4713d75bf51b65101e2d5ff9d0db3d6d269621806330','c2352c6cf6f53ebc3711f3d724eb8e544d6da27e372a1c1912da1193f6c951e7','0be8462d2156ec52a728f7a4f96e627caca68d6d252ac2d72f85981a4e829221','eb55bdeb761e86c20b8252328e21ad29738a65a18a1c00ac8e6162b6d58d304f','3b316ba49a2bb7474893a569d1d6cbc8e726e623f4a939935f2eb75800adde8e'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $env:T $n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); $l=(Get-Item -LiteralPath $f).Length; if($g -eq $h[$i]){ 'S243 READBACK MATCH '+$n[$i]+' '+$l+' '+$g } else { 'S243 READBACK MISMATCH '+$n[$i]+' '+$l+' '+$g; $bad=1 } }; exit $bad" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL readback & exit /b 1)
echo S243 DELIVER COMPLETE
exit /b 0
