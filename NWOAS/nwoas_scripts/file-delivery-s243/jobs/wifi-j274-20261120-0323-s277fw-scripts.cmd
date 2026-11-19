@echo off
echo S243 DELIVER BEGIN 3 file(s) to C:\NWOAS\drop\wifi-j274-20261120-0323-s277fw
set "T=C:\NWOAS\drop\wifi-j274-20261120-0323-s277fw"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $p=$env:T; while($p){ if(Test-Path -LiteralPath $p){ $x=Get-Item -LiteralPath $p -Force; if(-not $x.PSIsContainer -or ($x.Attributes -band [IO.FileAttributes]::ReparsePoint)){ throw ('unsafe path component '+$p) } }; $p=Split-Path -Parent $p }; New-Item -ItemType Directory -Force -Path $env:T | Out-Null; 'S243 DIR READY '+$env:T" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL dir & exit /b 1)
"%~d0\NWGET243.EXE" 9 a474a523b140584148d6bed5456b8b82a8a05f53fffe082b7f1ea0e8f977d7fb "%T%\W17-etw-wlan-start.ps1" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 9 & exit /b 1)
"%~d0\NWGET243.EXE" 10 9ef57c270426aaf37a65a254397324669c2100c4496e3c5c35796a5f57608376 "%T%\W18-etw-wlan-stop.ps1" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 10 & exit /b 1)
"%~d0\NWGET243.EXE" 11 6a92e842915a27eef1e0865cf436ca2b65085d3387df9271ee469667d9e4f6b9 "%T%\W19-ping.ps1" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 11 & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('W17-etw-wlan-start.ps1','W18-etw-wlan-stop.ps1','W19-ping.ps1'); $h=@('a474a523b140584148d6bed5456b8b82a8a05f53fffe082b7f1ea0e8f977d7fb','9ef57c270426aaf37a65a254397324669c2100c4496e3c5c35796a5f57608376','6a92e842915a27eef1e0865cf436ca2b65085d3387df9271ee469667d9e4f6b9'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $env:T $n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); $l=(Get-Item -LiteralPath $f).Length; if($g -eq $h[$i]){ 'S243 READBACK MATCH '+$n[$i]+' '+$l+' '+$g } else { 'S243 READBACK MISMATCH '+$n[$i]+' '+$l+' '+$g; $bad=1 } }; exit $bad" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL readback & exit /b 1)
echo S243 DELIVER COMPLETE
exit /b 0
