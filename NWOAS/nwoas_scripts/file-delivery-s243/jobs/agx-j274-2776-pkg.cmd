@echo off
echo S243 DELIVER BEGIN 6 file(s) to C:\NWOAS\drop\agx-j274-2776
set "T=C:\NWOAS\drop\agx-j274-2776"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $p=$env:T; while($p){ if(Test-Path -LiteralPath $p){ $x=Get-Item -LiteralPath $p -Force; if(-not $x.PSIsContainer -or ($x.Attributes -band [IO.FileAttributes]::ReparsePoint)){ throw ('unsafe path component '+$p) } }; $p=Split-Path -Parent $p }; New-Item -ItemType Directory -Force -Path $env:T | Out-Null; 'S243 DIR READY '+$env:T" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL dir & exit /b 1)
"%~d0\NWGET243.EXE" 0 7fcdbd7d787662992f02559fa149bf813db382633a338b66100f1b245ea9eaa8 "%T%\AppleAgxRenderAdmission.sys" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 0 & exit /b 1)
"%~d0\NWGET243.EXE" 1 8502d1f11eabdf218fe8da6cd443782792e57db8403123e6d5e19e64c09abc77 "%T%\AppleAgxRenderAdmissionUmd.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 1 & exit /b 1)
"%~d0\NWGET243.EXE" 2 b6bd3eb9652758151a4124476d86fd2f9bb3b9aed8b79ac8d3be74d60b26ab8f "%T%\AppleAgxRenderAdmissionUmdArm64.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 2 & exit /b 1)
"%~d0\NWGET243.EXE" 3 d0bf3c00a2358699f3d15040971ae2c5471778a00c915852cfaca097450fad74 "%T%\AppleAgxRenderAdmissionUmdX64.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 3 & exit /b 1)
"%~d0\NWGET243.EXE" 4 6f9d076ce411978aada5f7853db52b04bfe05a20071f3bccfc0432be5e823fb0 "%T%\AppleAgxRenderAdmission.arm64x.inf" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 4 & exit /b 1)
"%~d0\NWGET243.EXE" 5 e747c36ab8797f213da0ed11fa23aeed0f6af1a3b9fd049501b24c8d33863023 "%T%\appleagxrenderadmission.cat" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 5 & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('AppleAgxRenderAdmission.sys','AppleAgxRenderAdmissionUmd.dll','AppleAgxRenderAdmissionUmdArm64.dll','AppleAgxRenderAdmissionUmdX64.dll','AppleAgxRenderAdmission.arm64x.inf','appleagxrenderadmission.cat'); $h=@('7fcdbd7d787662992f02559fa149bf813db382633a338b66100f1b245ea9eaa8','8502d1f11eabdf218fe8da6cd443782792e57db8403123e6d5e19e64c09abc77','b6bd3eb9652758151a4124476d86fd2f9bb3b9aed8b79ac8d3be74d60b26ab8f','d0bf3c00a2358699f3d15040971ae2c5471778a00c915852cfaca097450fad74','6f9d076ce411978aada5f7853db52b04bfe05a20071f3bccfc0432be5e823fb0','e747c36ab8797f213da0ed11fa23aeed0f6af1a3b9fd049501b24c8d33863023'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $env:T $n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); $l=(Get-Item -LiteralPath $f).Length; if($g -eq $h[$i]){ 'S243 READBACK MATCH '+$n[$i]+' '+$l+' '+$g } else { 'S243 READBACK MISMATCH '+$n[$i]+' '+$l+' '+$g; $bad=1 } }; exit $bad" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL readback & exit /b 1)
echo S243 DELIVER COMPLETE
exit /b 0
