@echo off
echo S243 DELIVER BEGIN 8 file(s) to C:\NWOAS\gpucov
set "T=C:\NWOAS\gpucov"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $p=$env:T; while($p){ if(Test-Path -LiteralPath $p){ $x=Get-Item -LiteralPath $p -Force; if(-not $x.PSIsContainer -or ($x.Attributes -band [IO.FileAttributes]::ReparsePoint)){ throw ('unsafe path component '+$p) } }; $p=Split-Path -Parent $p }; New-Item -ItemType Directory -Force -Path $env:T | Out-Null; 'S243 DIR READY '+$env:T" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL dir & exit /b 1)
"%~d0\NWGET243.EXE" 4 a517f292492d7752bb56fe54aa9a58e87216c4c8c505ac498ca7aa3df5fa07a6 "%T%\nwgpuprobe-arm64.exe" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 4 & exit /b 1)
"%~d0\NWGET243.EXE" 5 96cb77238b308662ae010cd13cfbba02792576b659a3e6c75d358783caafa562 "%T%\nwgpuprobe-x64.exe" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 5 & exit /b 1)
"%~d0\NWGET243.EXE" 6 24f4d8fe420aeed351092ab318c71f2ac92cb78434ecaae04dc8fa3ad17616b0 "%T%\webgl.html" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 6 & exit /b 1)
"%~d0\NWGET243.EXE" 7 cef1db063176f74852757f6fedd7c15acdc8fb7728a059a7902e3df87d81989e "%T%\usersess.ps1" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 7 & exit /b 1)
"%~d0\NWGET243.EXE" 8 099db9f772ec397ef5cbac189a41538e559388698014516bd8be0a415f5b69d0 "%T%\winstat.ps1" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 8 & exit /b 1)
"%~d0\NWGET243.EXE" 9 afc61cc312c1593bb2f04640c9f282a25cd67a4fbe0fa545b728a64fd2a59212 "%T%\cb26ui.ps1" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 9 & exit /b 1)
"%~d0\NWGET243.EXE" 10 a8f460fef3638aab97d3a9b6665db8c6cec1bedf7abcf531ef630b2fa9dadbe3 "%T%\edge.ps1" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 10 & exit /b 1)
"%~d0\NWGET243.EXE" 11 3df643a211a32b62f3faf48848abbce751cc4ffcb04915341f6bd482a2040634 "%T%\engines.ps1" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 11 & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('nwgpuprobe-arm64.exe','nwgpuprobe-x64.exe','webgl.html','usersess.ps1','winstat.ps1','cb26ui.ps1','edge.ps1','engines.ps1'); $h=@('a517f292492d7752bb56fe54aa9a58e87216c4c8c505ac498ca7aa3df5fa07a6','96cb77238b308662ae010cd13cfbba02792576b659a3e6c75d358783caafa562','24f4d8fe420aeed351092ab318c71f2ac92cb78434ecaae04dc8fa3ad17616b0','cef1db063176f74852757f6fedd7c15acdc8fb7728a059a7902e3df87d81989e','099db9f772ec397ef5cbac189a41538e559388698014516bd8be0a415f5b69d0','afc61cc312c1593bb2f04640c9f282a25cd67a4fbe0fa545b728a64fd2a59212','a8f460fef3638aab97d3a9b6665db8c6cec1bedf7abcf531ef630b2fa9dadbe3','3df643a211a32b62f3faf48848abbce751cc4ffcb04915341f6bd482a2040634'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $env:T $n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); $l=(Get-Item -LiteralPath $f).Length; if($g -eq $h[$i]){ 'S243 READBACK MATCH '+$n[$i]+' '+$l+' '+$g } else { 'S243 READBACK MISMATCH '+$n[$i]+' '+$l+' '+$g; $bad=1 } }; exit $bad" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL readback & exit /b 1)
echo S243 DELIVER COMPLETE
exit /b 0
