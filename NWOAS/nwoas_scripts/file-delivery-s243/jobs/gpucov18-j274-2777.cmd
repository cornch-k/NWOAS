@echo off
echo S243 DELIVER BEGIN 3 file(s) to C:\NWOAS\gpucov
set "T=C:\NWOAS\gpucov"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $p=$env:T; while($p){ if(Test-Path -LiteralPath $p){ $x=Get-Item -LiteralPath $p -Force; if(-not $x.PSIsContainer -or ($x.Attributes -band [IO.FileAttributes]::ReparsePoint)){ throw ('unsafe path component '+$p) } }; $p=Split-Path -Parent $p }; New-Item -ItemType Directory -Force -Path $env:T | Out-Null; 'S243 DIR READY '+$env:T" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL dir & exit /b 1)
"%~d0\NWGET243.EXE" 7 3b2c54fd10d85a0db227d1ffcc306f1143a0f10bed277639663cacdc442932c7 "%T%\nwgpuprobe15-arm64.exe" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 7 & exit /b 1)
"%~d0\NWGET243.EXE" 8 6bd568de915e9daeebcfe1d6335dbdb32823e5ef9106a58f5571e31035fb6318 "%T%\nwgpuprobe15-x64.exe" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 8 & exit /b 1)
"%~d0\NWGET243.EXE" 9 b0ba4ad490c235ecca66dbc6ed073b6dbfdb73c68e720f9e819867f3195d0288 "%T%\nwgpuprobe15-x86.exe" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 9 & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('nwgpuprobe15-arm64.exe','nwgpuprobe15-x64.exe','nwgpuprobe15-x86.exe'); $h=@('3b2c54fd10d85a0db227d1ffcc306f1143a0f10bed277639663cacdc442932c7','6bd568de915e9daeebcfe1d6335dbdb32823e5ef9106a58f5571e31035fb6318','b0ba4ad490c235ecca66dbc6ed073b6dbfdb73c68e720f9e819867f3195d0288'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $env:T $n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); $l=(Get-Item -LiteralPath $f).Length; if($g -eq $h[$i]){ 'S243 READBACK MATCH '+$n[$i]+' '+$l+' '+$g } else { 'S243 READBACK MISMATCH '+$n[$i]+' '+$l+' '+$g; $bad=1 } }; exit $bad" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL readback & exit /b 1)
echo S243 DELIVER COMPLETE
exit /b 0
