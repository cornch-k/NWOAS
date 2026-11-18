@echo off
echo S243 DELIVER BEGIN 4 file(s) to C:\NWOAS\drop\snd-j274-20261118-1412-bridge
set "T=C:\NWOAS\drop\snd-j274-20261118-1412-bridge"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $p=$env:T; while($p){ if(Test-Path -LiteralPath $p){ $x=Get-Item -LiteralPath $p -Force; if(-not $x.PSIsContainer -or ($x.Attributes -band [IO.FileAttributes]::ReparsePoint)){ throw ('unsafe path component '+$p) } }; $p=Split-Path -Parent $p }; New-Item -ItemType Directory -Force -Path $env:T | Out-Null; 'S243 DIR READY '+$env:T" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL dir & exit /b 1)
"%~d0\NWGET243.EXE" 0 c2a69f6c30b72457afc525ef63c3d5fb8a3c7e8657bb572106aa707a9598ca5f "%T%\NwoasAudio.inf" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 0 & exit /b 1)
"%~d0\NWGET243.EXE" 1 95f269473363675feb2bbaf0408e1075cdc5855b095333940a8786875c7395e9 "%T%\NwoasAudio.sys" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 1 & exit /b 1)
"%~d0\NWGET243.EXE" 2 920d0f721e95291f98f3984df31e226c46f16dd91374e80eb835ed45a414481b "%T%\nwoasaudio.cat" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 2 & exit /b 1)
"%~d0\NWGET243.EXE" 3 0a2835ac74bb1095c6788779b4d693e93c78753492a745077cf480477df8daaf "%T%\prot-tone.ps1" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 3 & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('NwoasAudio.inf','NwoasAudio.sys','nwoasaudio.cat','prot-tone.ps1'); $h=@('c2a69f6c30b72457afc525ef63c3d5fb8a3c7e8657bb572106aa707a9598ca5f','95f269473363675feb2bbaf0408e1075cdc5855b095333940a8786875c7395e9','920d0f721e95291f98f3984df31e226c46f16dd91374e80eb835ed45a414481b','0a2835ac74bb1095c6788779b4d693e93c78753492a745077cf480477df8daaf'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $env:T $n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); $l=(Get-Item -LiteralPath $f).Length; if($g -eq $h[$i]){ 'S243 READBACK MATCH '+$n[$i]+' '+$l+' '+$g } else { 'S243 READBACK MISMATCH '+$n[$i]+' '+$l+' '+$g; $bad=1 } }; exit $bad" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL readback & exit /b 1)
echo S243 DELIVER COMPLETE
exit /b 0
