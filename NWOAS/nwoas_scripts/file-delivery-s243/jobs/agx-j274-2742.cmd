@echo off
echo S243 DELIVER BEGIN 5 file(s) to C:\NWOAS\drop\agx-j274-2742
set "T=C:\NWOAS\drop\agx-j274-2742"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $p=$env:T; while($p){ if(Test-Path -LiteralPath $p){ $x=Get-Item -LiteralPath $p -Force; if(-not $x.PSIsContainer -or ($x.Attributes -band [IO.FileAttributes]::ReparsePoint)){ throw ('unsafe path component '+$p) } }; $p=Split-Path -Parent $p }; New-Item -ItemType Directory -Force -Path $env:T | Out-Null; 'S243 DIR READY '+$env:T" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL dir & exit /b 1)
"%~d0\NWGET243.EXE" 0 66d650b4d714eccb77c90b2cd84d630ebbae1a6f405a44a3c8c53a196e567020 "%T%\AppleAgxRenderAdmission.sys" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 0 & exit /b 1)
"%~d0\NWGET243.EXE" 1 416e3edd729d230828493836a92cb345803097459273f9dffb6d41003fd476cc "%T%\AppleAgxRenderAdmissionUmd.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 1 & exit /b 1)
"%~d0\NWGET243.EXE" 2 d72f8d4e9e4bff708e75ab24167863093e9378857fa4569ed8faa2882c4b27fa "%T%\AppleAgxRenderAdmission.arm64only.inf" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 2 & exit /b 1)
"%~d0\NWGET243.EXE" 3 4abcf56791b808f3b1a8d96588c85a63b99b8067c3cc0fc34c47f0c7754c439b "%T%\appleagxrenderadmission.cat" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 3 & exit /b 1)
"%~d0\NWGET243.EXE" 4 f7b551e680907726fc506fff9ad906735384f9beeb779fbecc214b09228f1c57 "%T%\AppleAgxRenderAdmission.cer" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 4 & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('AppleAgxRenderAdmission.sys','AppleAgxRenderAdmissionUmd.dll','AppleAgxRenderAdmission.arm64only.inf','appleagxrenderadmission.cat','AppleAgxRenderAdmission.cer'); $h=@('66d650b4d714eccb77c90b2cd84d630ebbae1a6f405a44a3c8c53a196e567020','416e3edd729d230828493836a92cb345803097459273f9dffb6d41003fd476cc','d72f8d4e9e4bff708e75ab24167863093e9378857fa4569ed8faa2882c4b27fa','4abcf56791b808f3b1a8d96588c85a63b99b8067c3cc0fc34c47f0c7754c439b','f7b551e680907726fc506fff9ad906735384f9beeb779fbecc214b09228f1c57'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $env:T $n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); $l=(Get-Item -LiteralPath $f).Length; if($g -eq $h[$i]){ 'S243 READBACK MATCH '+$n[$i]+' '+$l+' '+$g } else { 'S243 READBACK MISMATCH '+$n[$i]+' '+$l+' '+$g; $bad=1 } }; exit $bad" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL readback & exit /b 1)
echo S243 DELIVER COMPLETE
exit /b 0
