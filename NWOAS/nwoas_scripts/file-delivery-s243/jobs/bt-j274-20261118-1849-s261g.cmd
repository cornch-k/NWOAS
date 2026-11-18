@echo off
echo S243 DELIVER BEGIN 7 file(s) to C:\NWOAS\drop\bt-j274-20261118-1849-s261g
set "T=C:\NWOAS\drop\bt-j274-20261118-1849-s261g"
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
"%~d0\NWGET243.EXE" 4 352a45b76bf04c7ce58864cec0024653b1992fc4c236d299ffdeaabcf266817a "%T%\nwbt4378.cat" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 4 & exit /b 1)
"%~d0\NWGET243.EXE" 5 2964f295e25076a7fcc0be07ae732c18918a82f6078e5f84c340ce0e7ba1b844 "%T%\nwbt4378.inf" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 5 & exit /b 1)
"%~d0\NWGET243.EXE" 6 5f7dbff0fb7b32a1c0d64bdbce2a2859a4cbfe53fca6d028711cedb854d29a7b "%T%\nwbt4378.sys" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 6 & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('bt4378b1-atlantisb-m.bin','bt4378b1-atlantisb-m.ptb','bt4378b1-atlantisb-u.bin','bt4378b1-atlantisb-u.ptb','nwbt4378.cat','nwbt4378.inf','nwbt4378.sys'); $h=@('11d6aa02a247de70933abc1fce86e79521dbd32a756f46f375d558e1e32c07a4','75a2726d210d819d5f34e1ca7042ebcd23e2243337ebb8a1eabb97c28839e0a1','dc046e4df710f821dc15a713adbea3239f3efa4a38b8fc5be729c34c298a7b81','75a2726d210d819d5f34e1ca7042ebcd23e2243337ebb8a1eabb97c28839e0a1','352a45b76bf04c7ce58864cec0024653b1992fc4c236d299ffdeaabcf266817a','2964f295e25076a7fcc0be07ae732c18918a82f6078e5f84c340ce0e7ba1b844','5f7dbff0fb7b32a1c0d64bdbce2a2859a4cbfe53fca6d028711cedb854d29a7b'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $env:T $n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); $l=(Get-Item -LiteralPath $f).Length; if($g -eq $h[$i]){ 'S243 READBACK MATCH '+$n[$i]+' '+$l+' '+$g } else { 'S243 READBACK MISMATCH '+$n[$i]+' '+$l+' '+$g; $bad=1 } }; exit $bad" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL readback & exit /b 1)
echo S243 DELIVER COMPLETE
exit /b 0
