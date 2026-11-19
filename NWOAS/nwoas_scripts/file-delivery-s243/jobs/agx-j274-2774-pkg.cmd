@echo off
echo S243 DELIVER BEGIN 6 file(s) to C:\NWOAS\drop\agx-j274-2774
set "T=C:\NWOAS\drop\agx-j274-2774"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $p=$env:T; while($p){ if(Test-Path -LiteralPath $p){ $x=Get-Item -LiteralPath $p -Force; if(-not $x.PSIsContainer -or ($x.Attributes -band [IO.FileAttributes]::ReparsePoint)){ throw ('unsafe path component '+$p) } }; $p=Split-Path -Parent $p }; New-Item -ItemType Directory -Force -Path $env:T | Out-Null; 'S243 DIR READY '+$env:T" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL dir & exit /b 1)
"%~d0\NWGET243.EXE" 0 ef926ebf2226d0e63ca8dcd2a0d5f3a7ba43fc19ce471fceff79b95a709bcbe5 "%T%\AppleAgxRenderAdmission.sys" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 0 & exit /b 1)
"%~d0\NWGET243.EXE" 1 d8f5b7006c32aca88c97a14c8490f099f80f09913cb7a387b33547c8c2d6b923 "%T%\AppleAgxRenderAdmissionUmd.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 1 & exit /b 1)
"%~d0\NWGET243.EXE" 2 0e076329fcaf39bf450caf608fdd71e03c5a9d6564d2dc58fc542acf740d6819 "%T%\AppleAgxRenderAdmissionUmdArm64.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 2 & exit /b 1)
"%~d0\NWGET243.EXE" 3 3bd02d9c7da3116bdf14d089c4138da1d21e76869bde16ae98edcf95ff58c43b "%T%\AppleAgxRenderAdmissionUmdX64.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 3 & exit /b 1)
"%~d0\NWGET243.EXE" 4 e884378040af056b28e5bf4341da02169776fa503ba757fdeb3c2db20aa3e9b5 "%T%\AppleAgxRenderAdmission.arm64x.inf" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 4 & exit /b 1)
"%~d0\NWGET243.EXE" 5 813316feab6a0f1254c986b4c3646d38f02075c877fbd03032e372812a1e81d6 "%T%\appleagxrenderadmission.cat" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 5 & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('AppleAgxRenderAdmission.sys','AppleAgxRenderAdmissionUmd.dll','AppleAgxRenderAdmissionUmdArm64.dll','AppleAgxRenderAdmissionUmdX64.dll','AppleAgxRenderAdmission.arm64x.inf','appleagxrenderadmission.cat'); $h=@('ef926ebf2226d0e63ca8dcd2a0d5f3a7ba43fc19ce471fceff79b95a709bcbe5','d8f5b7006c32aca88c97a14c8490f099f80f09913cb7a387b33547c8c2d6b923','0e076329fcaf39bf450caf608fdd71e03c5a9d6564d2dc58fc542acf740d6819','3bd02d9c7da3116bdf14d089c4138da1d21e76869bde16ae98edcf95ff58c43b','e884378040af056b28e5bf4341da02169776fa503ba757fdeb3c2db20aa3e9b5','813316feab6a0f1254c986b4c3646d38f02075c877fbd03032e372812a1e81d6'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $env:T $n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); $l=(Get-Item -LiteralPath $f).Length; if($g -eq $h[$i]){ 'S243 READBACK MATCH '+$n[$i]+' '+$l+' '+$g } else { 'S243 READBACK MISMATCH '+$n[$i]+' '+$l+' '+$g; $bad=1 } }; exit $bad" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL readback & exit /b 1)
echo S243 DELIVER COMPLETE
exit /b 0
