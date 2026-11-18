@echo off
echo S243 DELIVER BEGIN 4 file(s) to C:\NWOAS\drop\agx-j274-2743
set "T=C:\NWOAS\drop\agx-j274-2743"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $p=$env:T; while($p){ if(Test-Path -LiteralPath $p){ $x=Get-Item -LiteralPath $p -Force; if(-not $x.PSIsContainer -or ($x.Attributes -band [IO.FileAttributes]::ReparsePoint)){ throw ('unsafe path component '+$p) } }; $p=Split-Path -Parent $p }; New-Item -ItemType Directory -Force -Path $env:T | Out-Null; 'S243 DIR READY '+$env:T" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL dir & exit /b 1)
"%~d0\NWGET243.EXE" 0 8d72b07ad71c03145eec6854cd60e7cb904b8bc90ddc80503cee42370d189b6b "%T%\AppleAgxRenderAdmission.sys" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 0 & exit /b 1)
"%~d0\NWGET243.EXE" 1 302e79e6003a260c6ad8091e607bd74b990c03eab29acac5ebb05e542b959375 "%T%\AppleAgxRenderAdmissionUmd.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 1 & exit /b 1)
"%~d0\NWGET243.EXE" 2 a9f23420c421c98680667d8d4a237d8a0e0fae0ddc6c8dc4b3bc946befc8f7da "%T%\AppleAgxRenderAdmission.arm64only.inf" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 2 & exit /b 1)
"%~d0\NWGET243.EXE" 3 927c356a8981ca3c6ebe6553956db97503bee97292d61ccac2a7901cd49741d1 "%T%\appleagxrenderadmission.cat" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 3 & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('AppleAgxRenderAdmission.sys','AppleAgxRenderAdmissionUmd.dll','AppleAgxRenderAdmission.arm64only.inf','appleagxrenderadmission.cat'); $h=@('8d72b07ad71c03145eec6854cd60e7cb904b8bc90ddc80503cee42370d189b6b','302e79e6003a260c6ad8091e607bd74b990c03eab29acac5ebb05e542b959375','a9f23420c421c98680667d8d4a237d8a0e0fae0ddc6c8dc4b3bc946befc8f7da','927c356a8981ca3c6ebe6553956db97503bee97292d61ccac2a7901cd49741d1'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $env:T $n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); $l=(Get-Item -LiteralPath $f).Length; if($g -eq $h[$i]){ 'S243 READBACK MATCH '+$n[$i]+' '+$l+' '+$g } else { 'S243 READBACK MISMATCH '+$n[$i]+' '+$l+' '+$g; $bad=1 } }; exit $bad" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL readback & exit /b 1)
echo S243 DELIVER COMPLETE
exit /b 0
