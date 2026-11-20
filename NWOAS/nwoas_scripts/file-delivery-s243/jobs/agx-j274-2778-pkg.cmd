@echo off
echo S243 DELIVER BEGIN 7 file(s) to C:\NWOAS\drop\agx-j274-2778
set "T=C:\NWOAS\drop\agx-j274-2778"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $p=$env:T; while($p){ if(Test-Path -LiteralPath $p){ $x=Get-Item -LiteralPath $p -Force; if(-not $x.PSIsContainer -or ($x.Attributes -band [IO.FileAttributes]::ReparsePoint)){ throw ('unsafe path component '+$p) } }; $p=Split-Path -Parent $p }; New-Item -ItemType Directory -Force -Path $env:T | Out-Null; 'S243 DIR READY '+$env:T" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL dir & exit /b 1)
"%~d0\NWGET243.EXE" 0 c06de374e1dfd20c05d8719b076ce197e5386abd81ef7444f789fc1bf7bc1cb0 "%T%\AppleAgxRenderAdmission.sys" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 0 & exit /b 1)
"%~d0\NWGET243.EXE" 1 77ae9e45fb5d06a5aed8ebe8e06f9a1d5e290db72f151ed09b085ae08261388d "%T%\AppleAgxRenderAdmissionUmd.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 1 & exit /b 1)
"%~d0\NWGET243.EXE" 2 e5e4f27b6c5e433a124803e3bdcf7eedc9ca1e878f3929f2b2674891f116629e "%T%\AppleAgxRenderAdmissionUmdArm64.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 2 & exit /b 1)
"%~d0\NWGET243.EXE" 3 529d46c272c016a83cef5305820350023dc2e484ed03b2f56340d06517702c96 "%T%\AppleAgxRenderAdmissionUmdX64.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 3 & exit /b 1)
"%~d0\NWGET243.EXE" 4 8c59885bf15c951601be4de77930a238e2352ca10ead1560e1ce28f0a2372fd4 "%T%\AppleAgxRenderAdmissionUmd32.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 4 & exit /b 1)
"%~d0\NWGET243.EXE" 5 e57fa00fcfe18fae9575db96bde8be74efba2aa56f72f68860209c245e18844d "%T%\AppleAgxRenderAdmission.arm64xwow.inf" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 5 & exit /b 1)
"%~d0\NWGET243.EXE" 6 edbb8708cd227f5b7748065fc3887101d54ef0434d37c5a930ae27946e4663de "%T%\appleagxrenderadmission.cat" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 6 & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('AppleAgxRenderAdmission.sys','AppleAgxRenderAdmissionUmd.dll','AppleAgxRenderAdmissionUmdArm64.dll','AppleAgxRenderAdmissionUmdX64.dll','AppleAgxRenderAdmissionUmd32.dll','AppleAgxRenderAdmission.arm64xwow.inf','appleagxrenderadmission.cat'); $h=@('c06de374e1dfd20c05d8719b076ce197e5386abd81ef7444f789fc1bf7bc1cb0','77ae9e45fb5d06a5aed8ebe8e06f9a1d5e290db72f151ed09b085ae08261388d','e5e4f27b6c5e433a124803e3bdcf7eedc9ca1e878f3929f2b2674891f116629e','529d46c272c016a83cef5305820350023dc2e484ed03b2f56340d06517702c96','8c59885bf15c951601be4de77930a238e2352ca10ead1560e1ce28f0a2372fd4','e57fa00fcfe18fae9575db96bde8be74efba2aa56f72f68860209c245e18844d','edbb8708cd227f5b7748065fc3887101d54ef0434d37c5a930ae27946e4663de'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $env:T $n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); $l=(Get-Item -LiteralPath $f).Length; if($g -eq $h[$i]){ 'S243 READBACK MATCH '+$n[$i]+' '+$l+' '+$g } else { 'S243 READBACK MISMATCH '+$n[$i]+' '+$l+' '+$g; $bad=1 } }; exit $bad" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL readback & exit /b 1)
echo S243 DELIVER COMPLETE
exit /b 0
