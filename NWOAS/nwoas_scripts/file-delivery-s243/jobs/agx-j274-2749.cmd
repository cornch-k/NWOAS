@echo off
echo S243 DELIVER BEGIN 4 file(s) to C:\NWOAS\drop\agx-j274-2749
set "T=C:\NWOAS\drop\agx-j274-2749"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $p=$env:T; while($p){ if(Test-Path -LiteralPath $p){ $x=Get-Item -LiteralPath $p -Force; if(-not $x.PSIsContainer -or ($x.Attributes -band [IO.FileAttributes]::ReparsePoint)){ throw ('unsafe path component '+$p) } }; $p=Split-Path -Parent $p }; New-Item -ItemType Directory -Force -Path $env:T | Out-Null; 'S243 DIR READY '+$env:T" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL dir & exit /b 1)
"%~d0\NWGET243.EXE" 0 dd188f018f94c878b384594e61591880b0a3811f51f8d8bfc7fee39afa80911b "%T%\AppleAgxRenderAdmission.sys" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 0 & exit /b 1)
"%~d0\NWGET243.EXE" 1 52ff97a8a19aff6ef8a17da6a53919fc5845fbac6e2c81ea58c0fc0c3dae2c66 "%T%\AppleAgxRenderAdmissionUmd.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 1 & exit /b 1)
"%~d0\NWGET243.EXE" 2 4d63797692a6feb5ba9aefc55c3a6c31f3ae4631f766564b35b5e1ff8258ac69 "%T%\AppleAgxRenderAdmission.arm64only.inf" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 2 & exit /b 1)
"%~d0\NWGET243.EXE" 3 c7722f976a5838604beeffd9343fb437b6c2f788a9aefc0b589fd6b28924474c "%T%\appleagxrenderadmission.cat" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 3 & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('AppleAgxRenderAdmission.sys','AppleAgxRenderAdmissionUmd.dll','AppleAgxRenderAdmission.arm64only.inf','appleagxrenderadmission.cat'); $h=@('dd188f018f94c878b384594e61591880b0a3811f51f8d8bfc7fee39afa80911b','52ff97a8a19aff6ef8a17da6a53919fc5845fbac6e2c81ea58c0fc0c3dae2c66','4d63797692a6feb5ba9aefc55c3a6c31f3ae4631f766564b35b5e1ff8258ac69','c7722f976a5838604beeffd9343fb437b6c2f788a9aefc0b589fd6b28924474c'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $env:T $n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); $l=(Get-Item -LiteralPath $f).Length; if($g -eq $h[$i]){ 'S243 READBACK MATCH '+$n[$i]+' '+$l+' '+$g } else { 'S243 READBACK MISMATCH '+$n[$i]+' '+$l+' '+$g; $bad=1 } }; exit $bad" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL readback & exit /b 1)
echo S243 DELIVER COMPLETE
exit /b 0
