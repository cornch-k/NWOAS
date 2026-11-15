@echo off
echo S243 DELIVER BEGIN 3 file(s) to C:\NWOAS\drop\gpu-j274
set "T=C:\NWOAS\drop\gpu-j274"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $p=$env:T; while($p){ if(Test-Path -LiteralPath $p){ $x=Get-Item -LiteralPath $p -Force; if(-not $x.PSIsContainer -or ($x.Attributes -band [IO.FileAttributes]::ReparsePoint)){ throw ('unsafe path component '+$p) } }; $p=Split-Path -Parent $p }; New-Item -ItemType Directory -Force -Path $env:T | Out-Null; 'S243 DIR READY '+$env:T" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL dir & exit /b 1)
"%~d0\NWGET243.EXE" 0 24e5a1a047e94f7cb435949c04468288648b08da1db5aa07f0461c177938ef44 "%T%\AppleAgxRenderAdmission.kmdonly.inf" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 0 & exit /b 1)
"%~d0\NWGET243.EXE" 1 1965872e848383311793a6383ca289748c42e50b3ff27a2c0798d9a4e70ee580 "%T%\AppleAgxRenderAdmission.sys" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 1 & exit /b 1)
"%~d0\NWGET243.EXE" 2 7e8f980d7e4436b5355f155610524af23025f16755e5d1334b6a528edcc1ce5a "%T%\appleagxrenderadmission.cat" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 2 & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('AppleAgxRenderAdmission.kmdonly.inf','AppleAgxRenderAdmission.sys','appleagxrenderadmission.cat'); $h=@('24e5a1a047e94f7cb435949c04468288648b08da1db5aa07f0461c177938ef44','1965872e848383311793a6383ca289748c42e50b3ff27a2c0798d9a4e70ee580','7e8f980d7e4436b5355f155610524af23025f16755e5d1334b6a528edcc1ce5a'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $env:T $n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); $l=(Get-Item -LiteralPath $f).Length; if($g -eq $h[$i]){ 'S243 READBACK MATCH '+$n[$i]+' '+$l+' '+$g } else { 'S243 READBACK MISMATCH '+$n[$i]+' '+$l+' '+$g; $bad=1 } }; exit $bad" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL readback & exit /b 1)
echo S243 DELIVER COMPLETE
exit /b 0
