@echo off
echo S243 DELIVER BEGIN 4 file(s) to C:\NWOAS\drop\agx-j274-2757
set "T=C:\NWOAS\drop\agx-j274-2757"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $p=$env:T; while($p){ if(Test-Path -LiteralPath $p){ $x=Get-Item -LiteralPath $p -Force; if(-not $x.PSIsContainer -or ($x.Attributes -band [IO.FileAttributes]::ReparsePoint)){ throw ('unsafe path component '+$p) } }; $p=Split-Path -Parent $p }; New-Item -ItemType Directory -Force -Path $env:T | Out-Null; 'S243 DIR READY '+$env:T" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL dir & exit /b 1)
"%~d0\NWGET243.EXE" 0 008e80bc9e59db0935ecaff6db9c37e93d37697c18b193ca55f6394787736643 "%T%\AppleAgxRenderAdmission.sys" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 0 & exit /b 1)
"%~d0\NWGET243.EXE" 1 769850783a2aa8adabe47e2da29cef1ba501bac12142b50a3d4b4be464e47c4d "%T%\AppleAgxRenderAdmissionUmd.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 1 & exit /b 1)
"%~d0\NWGET243.EXE" 2 7cf1b95ca477c065c3c7f84a26d9a5d1162e55de166833d86a350e28da54f7d9 "%T%\AppleAgxRenderAdmission.arm64only.inf" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 2 & exit /b 1)
"%~d0\NWGET243.EXE" 3 37ace93f0ce1d884fd7095f9a814fa3dba42ebc0fd6ac59dbed9856149b80b0a "%T%\appleagxrenderadmission.cat" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 3 & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('AppleAgxRenderAdmission.sys','AppleAgxRenderAdmissionUmd.dll','AppleAgxRenderAdmission.arm64only.inf','appleagxrenderadmission.cat'); $h=@('008e80bc9e59db0935ecaff6db9c37e93d37697c18b193ca55f6394787736643','769850783a2aa8adabe47e2da29cef1ba501bac12142b50a3d4b4be464e47c4d','7cf1b95ca477c065c3c7f84a26d9a5d1162e55de166833d86a350e28da54f7d9','37ace93f0ce1d884fd7095f9a814fa3dba42ebc0fd6ac59dbed9856149b80b0a'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $env:T $n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); $l=(Get-Item -LiteralPath $f).Length; if($g -eq $h[$i]){ 'S243 READBACK MATCH '+$n[$i]+' '+$l+' '+$g } else { 'S243 READBACK MISMATCH '+$n[$i]+' '+$l+' '+$g; $bad=1 } }; exit $bad" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL readback & exit /b 1)
echo S243 DELIVER COMPLETE
exit /b 0
