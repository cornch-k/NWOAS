@echo off
echo S243 DELIVER BEGIN 7 file(s) to C:\NWOAS\drop\agx-j274-2777
set "T=C:\NWOAS\drop\agx-j274-2777"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $p=$env:T; while($p){ if(Test-Path -LiteralPath $p){ $x=Get-Item -LiteralPath $p -Force; if(-not $x.PSIsContainer -or ($x.Attributes -band [IO.FileAttributes]::ReparsePoint)){ throw ('unsafe path component '+$p) } }; $p=Split-Path -Parent $p }; New-Item -ItemType Directory -Force -Path $env:T | Out-Null; 'S243 DIR READY '+$env:T" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL dir & exit /b 1)
"%~d0\NWGET243.EXE" 0 f256dbc4249f5665ffae7d1bfe2c34aa0b6da201f5d3c69878c17de91c60f8cf "%T%\AppleAgxRenderAdmission.sys" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 0 & exit /b 1)
"%~d0\NWGET243.EXE" 1 6e7a375b2a738f1acbbca00edd7ee0a9b5156b189d01a38a71bc932bc90b404f "%T%\AppleAgxRenderAdmissionUmd.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 1 & exit /b 1)
"%~d0\NWGET243.EXE" 2 c3e2a3d851da501703d32fa0b2e20d741c1acb2a4c1e1e74d14aa0d12c6130ac "%T%\AppleAgxRenderAdmissionUmdArm64.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 2 & exit /b 1)
"%~d0\NWGET243.EXE" 3 fc1475acddb21827280b0025657225eba624653bec755db55cebe6e448977178 "%T%\AppleAgxRenderAdmissionUmdX64.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 3 & exit /b 1)
"%~d0\NWGET243.EXE" 4 5601111d01fdf721a3165668eb155667d4af9e1919e758641c273e53556b2a9a "%T%\AppleAgxRenderAdmissionUmd32.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 4 & exit /b 1)
"%~d0\NWGET243.EXE" 5 2ea86858e37a3d6a86c129604e324a8d129e042cd6f161927e252ed27a74d193 "%T%\AppleAgxRenderAdmission.arm64xwow.inf" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 5 & exit /b 1)
"%~d0\NWGET243.EXE" 6 bce86a72e4c9179c6d346d56d04cbba37f2807f7bcf892882c4360292fe844cf "%T%\appleagxrenderadmission.cat" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 6 & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('AppleAgxRenderAdmission.sys','AppleAgxRenderAdmissionUmd.dll','AppleAgxRenderAdmissionUmdArm64.dll','AppleAgxRenderAdmissionUmdX64.dll','AppleAgxRenderAdmissionUmd32.dll','AppleAgxRenderAdmission.arm64xwow.inf','appleagxrenderadmission.cat'); $h=@('f256dbc4249f5665ffae7d1bfe2c34aa0b6da201f5d3c69878c17de91c60f8cf','6e7a375b2a738f1acbbca00edd7ee0a9b5156b189d01a38a71bc932bc90b404f','c3e2a3d851da501703d32fa0b2e20d741c1acb2a4c1e1e74d14aa0d12c6130ac','fc1475acddb21827280b0025657225eba624653bec755db55cebe6e448977178','5601111d01fdf721a3165668eb155667d4af9e1919e758641c273e53556b2a9a','2ea86858e37a3d6a86c129604e324a8d129e042cd6f161927e252ed27a74d193','bce86a72e4c9179c6d346d56d04cbba37f2807f7bcf892882c4360292fe844cf'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $env:T $n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); $l=(Get-Item -LiteralPath $f).Length; if($g -eq $h[$i]){ 'S243 READBACK MATCH '+$n[$i]+' '+$l+' '+$g } else { 'S243 READBACK MISMATCH '+$n[$i]+' '+$l+' '+$g; $bad=1 } }; exit $bad" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL readback & exit /b 1)
echo S243 DELIVER COMPLETE
exit /b 0
