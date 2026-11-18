@echo off
echo S243 DELIVER BEGIN 7 file(s) to C:\NWOAS\drop\bt-j274-20261118-1806-s261e
set "T=C:\NWOAS\drop\bt-j274-20261118-1806-s261e"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $p=$env:T; while($p){ if(Test-Path -LiteralPath $p){ $x=Get-Item -LiteralPath $p -Force; if(-not $x.PSIsContainer -or ($x.Attributes -band [IO.FileAttributes]::ReparsePoint)){ throw ('unsafe path component '+$p) } }; $p=Split-Path -Parent $p }; New-Item -ItemType Directory -Force -Path $env:T | Out-Null; 'S243 DIR READY '+$env:T" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL dir & exit /b 1)
"%~d0\NWGET243.EXE" 0 11d6aa02a247de70933abc1fce86e79521dbd32a756f46f375d558e1e32c07a4 "%T%\bt4378b1-atlantisb-m.bin" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 0 & exit /b 1)
"%~d0\NWGET243.EXE" 1 75a2726d210d819d5f34e1ca7042ebcd23e2243337ebb8a1eabb97c28839e0a1 "%T%\bt4378b1-atlantisb-m.ptb" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 1 & exit /b 1)
"%~d0\NWGET243.EXE" 2 dc046e4df710f821dc15a713adbea3239f3efa4a38b8fc5be729c34c298a7b81 "%T%\bt4378b1-atlantisb-u.bin" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 2 & exit /b 1)
"%~d0\NWGET243.EXE" 3 75a2726d210d819d5f34e1ca7042ebcd23e2243337ebb8a1eabb97c28839e0a1 "%T%\bt4378b1-atlantisb-u.ptb" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 3 & exit /b 1)
"%~d0\NWGET243.EXE" 4 0562a5d9fae1a425159ced3baf0f2793c594721f3d870c821b525764ccac55f4 "%T%\nwbt4378.cat" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 4 & exit /b 1)
"%~d0\NWGET243.EXE" 5 6eaad0eb4f04fc1532101f5a745cd7c3800047cf2861ad0d1e0d00143051aa3e "%T%\nwbt4378.inf" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 5 & exit /b 1)
"%~d0\NWGET243.EXE" 6 2e0448fa369c3c99a99c48afdb357ab53b1065283763d5efef25cf1bda74f177 "%T%\nwbt4378.sys" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 6 & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('bt4378b1-atlantisb-m.bin','bt4378b1-atlantisb-m.ptb','bt4378b1-atlantisb-u.bin','bt4378b1-atlantisb-u.ptb','nwbt4378.cat','nwbt4378.inf','nwbt4378.sys'); $h=@('11d6aa02a247de70933abc1fce86e79521dbd32a756f46f375d558e1e32c07a4','75a2726d210d819d5f34e1ca7042ebcd23e2243337ebb8a1eabb97c28839e0a1','dc046e4df710f821dc15a713adbea3239f3efa4a38b8fc5be729c34c298a7b81','75a2726d210d819d5f34e1ca7042ebcd23e2243337ebb8a1eabb97c28839e0a1','0562a5d9fae1a425159ced3baf0f2793c594721f3d870c821b525764ccac55f4','6eaad0eb4f04fc1532101f5a745cd7c3800047cf2861ad0d1e0d00143051aa3e','2e0448fa369c3c99a99c48afdb357ab53b1065283763d5efef25cf1bda74f177'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $env:T $n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); $l=(Get-Item -LiteralPath $f).Length; if($g -eq $h[$i]){ 'S243 READBACK MATCH '+$n[$i]+' '+$l+' '+$g } else { 'S243 READBACK MISMATCH '+$n[$i]+' '+$l+' '+$g; $bad=1 } }; exit $bad" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL readback & exit /b 1)
echo S243 DELIVER COMPLETE
exit /b 0
