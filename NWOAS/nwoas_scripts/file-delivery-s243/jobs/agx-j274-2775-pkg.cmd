@echo off
echo S243 DELIVER BEGIN 6 file(s) to C:\NWOAS\drop\agx-j274-2775
set "T=C:\NWOAS\drop\agx-j274-2775"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $p=$env:T; while($p){ if(Test-Path -LiteralPath $p){ $x=Get-Item -LiteralPath $p -Force; if(-not $x.PSIsContainer -or ($x.Attributes -band [IO.FileAttributes]::ReparsePoint)){ throw ('unsafe path component '+$p) } }; $p=Split-Path -Parent $p }; New-Item -ItemType Directory -Force -Path $env:T | Out-Null; 'S243 DIR READY '+$env:T" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL dir & exit /b 1)
"%~d0\NWGET243.EXE" 0 2c451f39a30c7d98c4b309bd130ad8f7cb397d15834ab7b57eaa5636685128e3 "%T%\AppleAgxRenderAdmission.sys" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 0 & exit /b 1)
"%~d0\NWGET243.EXE" 1 9b3d5876b97ceeb5d873dc67231305aee7db2de52bbaa37cd07ee3125e5ec1de "%T%\AppleAgxRenderAdmissionUmd.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 1 & exit /b 1)
"%~d0\NWGET243.EXE" 2 4210c890b80144f549f5535134fe896873e323a9782871216f9ab234f5351687 "%T%\AppleAgxRenderAdmissionUmdArm64.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 2 & exit /b 1)
"%~d0\NWGET243.EXE" 3 825cf0f079683359a6d9dfcd716eb7280667d7ac7c47361d76bf9d84118d9fd2 "%T%\AppleAgxRenderAdmissionUmdX64.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 3 & exit /b 1)
"%~d0\NWGET243.EXE" 4 2352da497e6d7c0aad893b54588edafa4ba8996d3df8e5e26ee7ef2e2edda4ca "%T%\AppleAgxRenderAdmission.arm64x.inf" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 4 & exit /b 1)
"%~d0\NWGET243.EXE" 5 a261e41ba6c2e3c0bc158f9edc1a6e09839c9d8b13fafc7186f561a0cdcdcbfb "%T%\appleagxrenderadmission.cat" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 5 & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('AppleAgxRenderAdmission.sys','AppleAgxRenderAdmissionUmd.dll','AppleAgxRenderAdmissionUmdArm64.dll','AppleAgxRenderAdmissionUmdX64.dll','AppleAgxRenderAdmission.arm64x.inf','appleagxrenderadmission.cat'); $h=@('2c451f39a30c7d98c4b309bd130ad8f7cb397d15834ab7b57eaa5636685128e3','9b3d5876b97ceeb5d873dc67231305aee7db2de52bbaa37cd07ee3125e5ec1de','4210c890b80144f549f5535134fe896873e323a9782871216f9ab234f5351687','825cf0f079683359a6d9dfcd716eb7280667d7ac7c47361d76bf9d84118d9fd2','2352da497e6d7c0aad893b54588edafa4ba8996d3df8e5e26ee7ef2e2edda4ca','a261e41ba6c2e3c0bc158f9edc1a6e09839c9d8b13fafc7186f561a0cdcdcbfb'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $env:T $n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); $l=(Get-Item -LiteralPath $f).Length; if($g -eq $h[$i]){ 'S243 READBACK MATCH '+$n[$i]+' '+$l+' '+$g } else { 'S243 READBACK MISMATCH '+$n[$i]+' '+$l+' '+$g; $bad=1 } }; exit $bad" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL readback & exit /b 1)
echo S243 DELIVER COMPLETE
exit /b 0
