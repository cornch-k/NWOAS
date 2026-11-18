@echo off
echo S243 DELIVER BEGIN 4 file(s) to C:\NWOAS\drop\agx-j274-2751
set "T=C:\NWOAS\drop\agx-j274-2751"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $p=$env:T; while($p){ if(Test-Path -LiteralPath $p){ $x=Get-Item -LiteralPath $p -Force; if(-not $x.PSIsContainer -or ($x.Attributes -band [IO.FileAttributes]::ReparsePoint)){ throw ('unsafe path component '+$p) } }; $p=Split-Path -Parent $p }; New-Item -ItemType Directory -Force -Path $env:T | Out-Null; 'S243 DIR READY '+$env:T" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL dir & exit /b 1)
"%~d0\NWGET243.EXE" 0 bff903cab7807ae9507286af097445bfaf0550e870178e0d82c0f58c915fc323 "%T%\AppleAgxRenderAdmission.sys" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 0 & exit /b 1)
"%~d0\NWGET243.EXE" 1 0722f6cc49cd7951937afd4a0a510be51d5fcbbff76ef66879d16a01e4f6a18a "%T%\AppleAgxRenderAdmissionUmd.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 1 & exit /b 1)
"%~d0\NWGET243.EXE" 2 19907fa8ec9eb09bd4fbe2e5ad68acfcf5574cf9da7c42328eb7932a7d4d88ec "%T%\AppleAgxRenderAdmission.arm64only.inf" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 2 & exit /b 1)
"%~d0\NWGET243.EXE" 3 f4bc5d4b59b44b97d5d88e562dc2abb126b51fa4580a762d338577595740429d "%T%\appleagxrenderadmission.cat" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 3 & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('AppleAgxRenderAdmission.sys','AppleAgxRenderAdmissionUmd.dll','AppleAgxRenderAdmission.arm64only.inf','appleagxrenderadmission.cat'); $h=@('bff903cab7807ae9507286af097445bfaf0550e870178e0d82c0f58c915fc323','0722f6cc49cd7951937afd4a0a510be51d5fcbbff76ef66879d16a01e4f6a18a','19907fa8ec9eb09bd4fbe2e5ad68acfcf5574cf9da7c42328eb7932a7d4d88ec','f4bc5d4b59b44b97d5d88e562dc2abb126b51fa4580a762d338577595740429d'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $env:T $n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); $l=(Get-Item -LiteralPath $f).Length; if($g -eq $h[$i]){ 'S243 READBACK MATCH '+$n[$i]+' '+$l+' '+$g } else { 'S243 READBACK MISMATCH '+$n[$i]+' '+$l+' '+$g; $bad=1 } }; exit $bad" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL readback & exit /b 1)
echo S243 DELIVER COMPLETE
exit /b 0
