@echo off
echo S243 DELIVER BEGIN 3 file(s) to C:\NWOAS\drop\wifi-j274-20261119-0429-s258m
set "T=C:\NWOAS\drop\wifi-j274-20261119-0429-s258m"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $p=$env:T; while($p){ if(Test-Path -LiteralPath $p){ $x=Get-Item -LiteralPath $p -Force; if(-not $x.PSIsContainer -or ($x.Attributes -band [IO.FileAttributes]::ReparsePoint)){ throw ('unsafe path component '+$p) } }; $p=Split-Path -Parent $p }; New-Item -ItemType Directory -Force -Path $env:T | Out-Null; 'S243 DIR READY '+$env:T" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL dir & exit /b 1)
"%~d0\NWGET243.EXE" 9 a474a523b140584148d6bed5456b8b82a8a05f53fffe082b7f1ea0e8f977d7fb "%T%\W17-etw-wlan-start.ps1" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 9 & exit /b 1)
"%~d0\NWGET243.EXE" 10 ff976ab9615da768a8c25dbf7f337cd655e85b74438158e751cfa01a2f6ab374 "%T%\W18-etw-wlan-stop.ps1" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 10 & exit /b 1)
"%~d0\NWGET243.EXE" 11 a3a8b46ea29c10e83675a99342b88ec819eff0940fb792c39490c7067a0aa806 "%T%\W19-ping.ps1" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 11 & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('W17-etw-wlan-start.ps1','W18-etw-wlan-stop.ps1','W19-ping.ps1'); $h=@('a474a523b140584148d6bed5456b8b82a8a05f53fffe082b7f1ea0e8f977d7fb','ff976ab9615da768a8c25dbf7f337cd655e85b74438158e751cfa01a2f6ab374','a3a8b46ea29c10e83675a99342b88ec819eff0940fb792c39490c7067a0aa806'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $env:T $n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); $l=(Get-Item -LiteralPath $f).Length; if($g -eq $h[$i]){ 'S243 READBACK MATCH '+$n[$i]+' '+$l+' '+$g } else { 'S243 READBACK MISMATCH '+$n[$i]+' '+$l+' '+$g; $bad=1 } }; exit $bad" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL readback & exit /b 1)
echo S243 DELIVER COMPLETE
exit /b 0
