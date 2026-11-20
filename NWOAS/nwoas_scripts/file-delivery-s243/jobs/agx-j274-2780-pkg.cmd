@echo off
echo S243 DELIVER BEGIN 7 file(s) to C:\NWOAS\drop\agx-j274-2780
set "T=C:\NWOAS\drop\agx-j274-2780"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $p=$env:T; while($p){ if(Test-Path -LiteralPath $p){ $x=Get-Item -LiteralPath $p -Force; if(-not $x.PSIsContainer -or ($x.Attributes -band [IO.FileAttributes]::ReparsePoint)){ throw ('unsafe path component '+$p) } }; $p=Split-Path -Parent $p }; New-Item -ItemType Directory -Force -Path $env:T | Out-Null; 'S243 DIR READY '+$env:T" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL dir & exit /b 1)
"%~d0\NWGET243.EXE" 0 14b690c91cce55f3bdd7bb97095fcca8aac44bee0a01eac2477a63b739bf6c93 "%T%\AppleAgxRenderAdmission.sys" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 0 & exit /b 1)
"%~d0\NWGET243.EXE" 1 6ebfbf12cd56c9664068af80a25227188bce5c6322210e3a9118b8534550d59f "%T%\AppleAgxRenderAdmissionUmd.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 1 & exit /b 1)
"%~d0\NWGET243.EXE" 2 024da86beceb5f3b321a1cbb50466430bfab0a6544a32ce34e5aeecb64009524 "%T%\AppleAgxRenderAdmissionUmdArm64.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 2 & exit /b 1)
"%~d0\NWGET243.EXE" 3 229b684f01f54ad8fc3126bb7b2fc9e5e53b142770390456911727cf0738e0bd "%T%\AppleAgxRenderAdmissionUmdX64.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 3 & exit /b 1)
"%~d0\NWGET243.EXE" 4 806eb20c4cee8eced9cabf754c7d50b7a79c999fd07ab3019f03d73c1b217f08 "%T%\AppleAgxRenderAdmissionUmd32.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 4 & exit /b 1)
"%~d0\NWGET243.EXE" 5 81f461edbeeadfc3a516f03ba6ebf3769a33afdfd5ff73edc09ca0664c3fd772 "%T%\AppleAgxRenderAdmission.arm64xwow.inf" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 5 & exit /b 1)
"%~d0\NWGET243.EXE" 6 4b2b607d0240e12a129cc3a4bf7b92614bf868651e8d026540a9fee9add33cbd "%T%\appleagxrenderadmission.cat" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 6 & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('AppleAgxRenderAdmission.sys','AppleAgxRenderAdmissionUmd.dll','AppleAgxRenderAdmissionUmdArm64.dll','AppleAgxRenderAdmissionUmdX64.dll','AppleAgxRenderAdmissionUmd32.dll','AppleAgxRenderAdmission.arm64xwow.inf','appleagxrenderadmission.cat'); $h=@('14b690c91cce55f3bdd7bb97095fcca8aac44bee0a01eac2477a63b739bf6c93','6ebfbf12cd56c9664068af80a25227188bce5c6322210e3a9118b8534550d59f','024da86beceb5f3b321a1cbb50466430bfab0a6544a32ce34e5aeecb64009524','229b684f01f54ad8fc3126bb7b2fc9e5e53b142770390456911727cf0738e0bd','806eb20c4cee8eced9cabf754c7d50b7a79c999fd07ab3019f03d73c1b217f08','81f461edbeeadfc3a516f03ba6ebf3769a33afdfd5ff73edc09ca0664c3fd772','4b2b607d0240e12a129cc3a4bf7b92614bf868651e8d026540a9fee9add33cbd'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $env:T $n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); $l=(Get-Item -LiteralPath $f).Length; if($g -eq $h[$i]){ 'S243 READBACK MATCH '+$n[$i]+' '+$l+' '+$g } else { 'S243 READBACK MISMATCH '+$n[$i]+' '+$l+' '+$g; $bad=1 } }; exit $bad" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL readback & exit /b 1)
echo S243 DELIVER COMPLETE
exit /b 0
