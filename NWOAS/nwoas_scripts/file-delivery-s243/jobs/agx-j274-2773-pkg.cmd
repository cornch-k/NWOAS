@echo off
echo S243 DELIVER BEGIN 6 file(s) to C:\NWOAS\drop\agx-j274-2773
set "T=C:\NWOAS\drop\agx-j274-2773"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $p=$env:T; while($p){ if(Test-Path -LiteralPath $p){ $x=Get-Item -LiteralPath $p -Force; if(-not $x.PSIsContainer -or ($x.Attributes -band [IO.FileAttributes]::ReparsePoint)){ throw ('unsafe path component '+$p) } }; $p=Split-Path -Parent $p }; New-Item -ItemType Directory -Force -Path $env:T | Out-Null; 'S243 DIR READY '+$env:T" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL dir & exit /b 1)
"%~d0\NWGET243.EXE" 0 28cb665ecded4afc07f38f599c80af6d5410b9dc9697ae6b00c95e0006ea667a "%T%\AppleAgxRenderAdmission.sys" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 0 & exit /b 1)
"%~d0\NWGET243.EXE" 1 6f10255356dd2956864f5bfdb8a59c25fe0501d3474459f6f66b04f9db562fbd "%T%\AppleAgxRenderAdmissionUmd.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 1 & exit /b 1)
"%~d0\NWGET243.EXE" 2 71e99344179800b6993691643fec1e88ef37de0d06ad8187a11934fad5132a58 "%T%\AppleAgxRenderAdmissionUmdArm64.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 2 & exit /b 1)
"%~d0\NWGET243.EXE" 3 712d067f0db4fc0684a9ff6508b94582286f746547d648b2c4c68ff633b1b064 "%T%\AppleAgxRenderAdmissionUmdX64.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 3 & exit /b 1)
"%~d0\NWGET243.EXE" 4 56fd01224e4e60dea1d1a7bbc665c7f293c100886af90eb6b483e8fa10decbd1 "%T%\AppleAgxRenderAdmission.arm64x.inf" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 4 & exit /b 1)
"%~d0\NWGET243.EXE" 5 6d137825f912ef65d1d5ba2efcd598c9503a9ee369df686faeedc4eaed525653 "%T%\appleagxrenderadmission.cat" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 5 & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('AppleAgxRenderAdmission.sys','AppleAgxRenderAdmissionUmd.dll','AppleAgxRenderAdmissionUmdArm64.dll','AppleAgxRenderAdmissionUmdX64.dll','AppleAgxRenderAdmission.arm64x.inf','appleagxrenderadmission.cat'); $h=@('28cb665ecded4afc07f38f599c80af6d5410b9dc9697ae6b00c95e0006ea667a','6f10255356dd2956864f5bfdb8a59c25fe0501d3474459f6f66b04f9db562fbd','71e99344179800b6993691643fec1e88ef37de0d06ad8187a11934fad5132a58','712d067f0db4fc0684a9ff6508b94582286f746547d648b2c4c68ff633b1b064','56fd01224e4e60dea1d1a7bbc665c7f293c100886af90eb6b483e8fa10decbd1','6d137825f912ef65d1d5ba2efcd598c9503a9ee369df686faeedc4eaed525653'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $env:T $n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); $l=(Get-Item -LiteralPath $f).Length; if($g -eq $h[$i]){ 'S243 READBACK MATCH '+$n[$i]+' '+$l+' '+$g } else { 'S243 READBACK MISMATCH '+$n[$i]+' '+$l+' '+$g; $bad=1 } }; exit $bad" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL readback & exit /b 1)
echo S243 DELIVER COMPLETE
exit /b 0
