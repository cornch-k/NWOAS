@echo off
echo S243 DELIVER BEGIN 6 file(s) to C:\NWOAS\drop\esp-4-1
set "T=C:\NWOAS\drop\esp-4-1"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $p=$env:T; while($p){ if(Test-Path -LiteralPath $p){ $x=Get-Item -LiteralPath $p -Force; if(-not $x.PSIsContainer -or ($x.Attributes -band [IO.FileAttributes]::ReparsePoint)){ throw ('unsafe path component '+$p) } }; $p=Split-Path -Parent $p }; New-Item -ItemType Directory -Force -Path $env:T | Out-Null; 'S243 DIR READY '+$env:T" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL dir & exit /b 1)
"%~d0\NWGET243.EXE" 0 80a13ca257fcf1d04676e08355b78ffec57cf82fbbbf3b3395f3524daa187e48 "%T%\boot.bin" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 0 & exit /b 1)
"%~d0\NWGET243.EXE" 1 d8756b7b00fe4aa7b2e4fd922a6727c87a1df1cdb32acab56b374d9043883327 "%T%\cleanbp-local.sh" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 1 & exit /b 1)
"%~d0\NWGET243.EXE" 2 0adddcf3151fc5fc29e887b41fe0ed23c582303841f33373f8e6cd9810276909 "%T%\go1.sh" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 2 & exit /b 1)
"%~d0\NWGET243.EXE" 3 26428b579ba117ae3b26c8d914461cc125b07140de1c1bf3393ff1a62fd0ab92 "%T%\nwoas-payload.bin" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 3 & exit /b 1)
"%~d0\NWGET243.EXE" 4 0191520648dfd5c18d7099b7bc7c2ad95c3aa6d0c21218dd1fb11843b35b6272 "%T%\nwoas-s264g.bin" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 4 & exit /b 1)
"%~d0\NWGET243.EXE" 5 8879def896f3577da90ec3d87caef3a549d2b4a0d9b7d6d3ee87b88ee4914778 "%T%\stage1.bin" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 5 & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('boot.bin','cleanbp-local.sh','go1.sh','nwoas-payload.bin','nwoas-s264g.bin','stage1.bin'); $h=@('80a13ca257fcf1d04676e08355b78ffec57cf82fbbbf3b3395f3524daa187e48','d8756b7b00fe4aa7b2e4fd922a6727c87a1df1cdb32acab56b374d9043883327','0adddcf3151fc5fc29e887b41fe0ed23c582303841f33373f8e6cd9810276909','26428b579ba117ae3b26c8d914461cc125b07140de1c1bf3393ff1a62fd0ab92','0191520648dfd5c18d7099b7bc7c2ad95c3aa6d0c21218dd1fb11843b35b6272','8879def896f3577da90ec3d87caef3a549d2b4a0d9b7d6d3ee87b88ee4914778'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $env:T $n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); $l=(Get-Item -LiteralPath $f).Length; if($g -eq $h[$i]){ 'S243 READBACK MATCH '+$n[$i]+' '+$l+' '+$g } else { 'S243 READBACK MISMATCH '+$n[$i]+' '+$l+' '+$g; $bad=1 } }; exit $bad" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL readback & exit /b 1)
echo S243 DELIVER COMPLETE
exit /b 0
