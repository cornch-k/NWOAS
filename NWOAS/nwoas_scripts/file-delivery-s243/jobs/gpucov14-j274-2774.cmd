@echo off
echo S243 DELIVER BEGIN 2 file(s) to C:\NWOAS\gpucov
set "T=C:\NWOAS\gpucov"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $p=$env:T; while($p){ if(Test-Path -LiteralPath $p){ $x=Get-Item -LiteralPath $p -Force; if(-not $x.PSIsContainer -or ($x.Attributes -band [IO.FileAttributes]::ReparsePoint)){ throw ('unsafe path component '+$p) } }; $p=Split-Path -Parent $p }; New-Item -ItemType Directory -Force -Path $env:T | Out-Null; 'S243 DIR READY '+$env:T" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL dir & exit /b 1)
"%~d0\NWGET243.EXE" 6 b62590cce32b0f2bd2a53f3b268675d9495f583efd316147561279570b3282fe "%T%\nwgpuprobe12-arm64.exe" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 6 & exit /b 1)
"%~d0\NWGET243.EXE" 7 c778a7f148eebc4d42261803e0a82af441bdcdfb944e603ca2a5d8d72bfad595 "%T%\nwgpuprobe12-x64.exe" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 7 & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('nwgpuprobe12-arm64.exe','nwgpuprobe12-x64.exe'); $h=@('b62590cce32b0f2bd2a53f3b268675d9495f583efd316147561279570b3282fe','c778a7f148eebc4d42261803e0a82af441bdcdfb944e603ca2a5d8d72bfad595'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $env:T $n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); $l=(Get-Item -LiteralPath $f).Length; if($g -eq $h[$i]){ 'S243 READBACK MATCH '+$n[$i]+' '+$l+' '+$g } else { 'S243 READBACK MISMATCH '+$n[$i]+' '+$l+' '+$g; $bad=1 } }; exit $bad" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL readback & exit /b 1)
echo S243 DELIVER COMPLETE
exit /b 0
