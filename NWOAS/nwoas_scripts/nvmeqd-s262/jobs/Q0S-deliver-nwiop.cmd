@echo off
echo S243 DELIVER BEGIN 1 file(s) to C:\NWOAS\s262
set "T=C:\NWOAS\s262"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $p=$env:T; while($p){ if(Test-Path -LiteralPath $p){ $x=Get-Item -LiteralPath $p -Force; if(-not $x.PSIsContainer -or ($x.Attributes -band [IO.FileAttributes]::ReparsePoint)){ throw ('unsafe path component '+$p) } }; $p=Split-Path -Parent $p }; New-Item -ItemType Directory -Force -Path $env:T | Out-Null; 'S243 DIR READY '+$env:T" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL dir & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$f=Join-Path $env:T 'NWIOP.EXE'; if(Test-Path -LiteralPath $f){ $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); if($g -eq 'bf98fbf67b8fb72aa83ee59b9fac55d5da0ea8f63bb3f4647f84c127a3916e5a'){ 'S243 READBACK MATCH NWIOP.EXE existing '+$g; exit 0 } else { 'S243 EXISTING MISMATCH '+$g; exit 2 } } else { exit 1 }" 2>&1
if %errorlevel%==0 (echo S243 DELIVER COMPLETE & exit /b 0)
if %errorlevel%==2 (echo S243 DELIVER FAIL existing-mismatch & exit /b 1)
"%~d0\NWGET243.EXE" 0 bf98fbf67b8fb72aa83ee59b9fac55d5da0ea8f63bb3f4647f84c127a3916e5a "%T%\NWIOP.EXE" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 0 & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('NWIOP.EXE'); $h=@('bf98fbf67b8fb72aa83ee59b9fac55d5da0ea8f63bb3f4647f84c127a3916e5a'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $env:T $n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); $l=(Get-Item -LiteralPath $f).Length; if($g -eq $h[$i]){ 'S243 READBACK MATCH '+$n[$i]+' '+$l+' '+$g } else { 'S243 READBACK MISMATCH '+$n[$i]+' '+$l+' '+$g; $bad=1 } }; exit $bad" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL readback & exit /b 1)
echo S243 DELIVER COMPLETE
exit /b 0
