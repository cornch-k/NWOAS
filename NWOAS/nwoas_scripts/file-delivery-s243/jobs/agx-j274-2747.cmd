@echo off
echo S243 DELIVER BEGIN 4 file(s) to C:\NWOAS\drop\agx-j274-2747
set "T=C:\NWOAS\drop\agx-j274-2747"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $p=$env:T; while($p){ if(Test-Path -LiteralPath $p){ $x=Get-Item -LiteralPath $p -Force; if(-not $x.PSIsContainer -or ($x.Attributes -band [IO.FileAttributes]::ReparsePoint)){ throw ('unsafe path component '+$p) } }; $p=Split-Path -Parent $p }; New-Item -ItemType Directory -Force -Path $env:T | Out-Null; 'S243 DIR READY '+$env:T" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL dir & exit /b 1)
"%~d0\NWGET243.EXE" 0 d4593ebdbf1e08de4804f009af65fbc933a53fb26a20edb427dcf0eff0aa00a5 "%T%\AppleAgxRenderAdmission.sys" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 0 & exit /b 1)
"%~d0\NWGET243.EXE" 1 11fcabdb007dcaf3adf94408f0931d518c3d58294eb864039d05d40fcc3f31bc "%T%\AppleAgxRenderAdmissionUmd.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 1 & exit /b 1)
"%~d0\NWGET243.EXE" 2 7921b3690c1ea05f99060aa319fb3601109f2b3bc8e19a3f56d6a45776b3158d "%T%\AppleAgxRenderAdmission.arm64only.inf" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 2 & exit /b 1)
"%~d0\NWGET243.EXE" 3 fc6745dae2aab1a027f39e1a3f19e298c73f47ae732a8dccd455af49f55a614f "%T%\appleagxrenderadmission.cat" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 3 & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('AppleAgxRenderAdmission.sys','AppleAgxRenderAdmissionUmd.dll','AppleAgxRenderAdmission.arm64only.inf','appleagxrenderadmission.cat'); $h=@('d4593ebdbf1e08de4804f009af65fbc933a53fb26a20edb427dcf0eff0aa00a5','11fcabdb007dcaf3adf94408f0931d518c3d58294eb864039d05d40fcc3f31bc','7921b3690c1ea05f99060aa319fb3601109f2b3bc8e19a3f56d6a45776b3158d','fc6745dae2aab1a027f39e1a3f19e298c73f47ae732a8dccd455af49f55a614f'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $env:T $n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); $l=(Get-Item -LiteralPath $f).Length; if($g -eq $h[$i]){ 'S243 READBACK MATCH '+$n[$i]+' '+$l+' '+$g } else { 'S243 READBACK MISMATCH '+$n[$i]+' '+$l+' '+$g; $bad=1 } }; exit $bad" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL readback & exit /b 1)
echo S243 DELIVER COMPLETE
exit /b 0
