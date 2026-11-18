@echo off
echo S243 DELIVER BEGIN 4 file(s) to C:\NWOAS\drop\snd-j274-20261118-1131
set "T=C:\NWOAS\drop\snd-j274-20261118-1131"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $p=$env:T; while($p){ if(Test-Path -LiteralPath $p){ $x=Get-Item -LiteralPath $p -Force; if(-not $x.PSIsContainer -or ($x.Attributes -band [IO.FileAttributes]::ReparsePoint)){ throw ('unsafe path component '+$p) } }; $p=Split-Path -Parent $p }; New-Item -ItemType Directory -Force -Path $env:T | Out-Null; 'S243 DIR READY '+$env:T" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL dir & exit /b 1)
"%~d0\NWGET243.EXE" 0 b3edb2ac5dd33fca17013b4b6261f6f97ec1a1253f82d6e4f7cb8253b6cafe60 "%T%\NwoasAudio.inf" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 0 & exit /b 1)
"%~d0\NWGET243.EXE" 1 dab60f39fd4cc372b3eeb39de725019239c786c9cadb5c6cbe12e6fd297232ff "%T%\NwoasAudio.sys" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 1 & exit /b 1)
"%~d0\NWGET243.EXE" 2 cafaf2e1d925e95a65753599f848a876d6e0750e1ea57d5fb9853955a51e7ac5 "%T%\nwoasaudio.cat" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 2 & exit /b 1)
"%~d0\NWGET243.EXE" 3 ff995663f61f8cf0f2eb8898858575dc7766feb48327fa66edb344ae4e3ad096 "%T%\nwoas-tone-quiet.wav" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 3 & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('NwoasAudio.inf','NwoasAudio.sys','nwoasaudio.cat','nwoas-tone-quiet.wav'); $h=@('b3edb2ac5dd33fca17013b4b6261f6f97ec1a1253f82d6e4f7cb8253b6cafe60','dab60f39fd4cc372b3eeb39de725019239c786c9cadb5c6cbe12e6fd297232ff','cafaf2e1d925e95a65753599f848a876d6e0750e1ea57d5fb9853955a51e7ac5','ff995663f61f8cf0f2eb8898858575dc7766feb48327fa66edb344ae4e3ad096'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $env:T $n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); $l=(Get-Item -LiteralPath $f).Length; if($g -eq $h[$i]){ 'S243 READBACK MATCH '+$n[$i]+' '+$l+' '+$g } else { 'S243 READBACK MISMATCH '+$n[$i]+' '+$l+' '+$g; $bad=1 } }; exit $bad" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL readback & exit /b 1)
echo S243 DELIVER COMPLETE
exit /b 0
