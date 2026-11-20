@echo off
echo S243 DELIVER BEGIN 9 file(s) to C:\NWOAS\drop\wifi-j274-20261120-0924-s278fw
set "T=C:\NWOAS\drop\wifi-j274-20261120-0924-s278fw"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $p=$env:T; while($p){ if(Test-Path -LiteralPath $p){ $x=Get-Item -LiteralPath $p -Force; if(-not $x.PSIsContainer -or ($x.Attributes -band [IO.FileAttributes]::ReparsePoint)){ throw ('unsafe path component '+$p) } }; $p=Split-Path -Parent $p }; New-Item -ItemType Directory -Force -Path $env:T | Out-Null; 'S243 DIR READY '+$env:T" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL dir & exit /b 1)
"%~d0\NWGET243.EXE" 0 5e8c0449664fa47d57f220aa75dd04ad6d5558adf157ddf20f447a9f4f5fb19e "%T%\P-atlantisb_M-RASP_V-m.txt" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 0 & exit /b 1)
"%~d0\NWGET243.EXE" 1 417a6014f4dad80d92f160748f43d2c50314ba67eefc15fa5eace378d82d943e "%T%\P-atlantisb_M-RASP_V-u.txt" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 1 & exit /b 1)
"%~d0\NWGET243.EXE" 2 322fbc347d7ee69d8db0bb8483c9e7e8ac711390b1f4dd951c12c5ec153f4ab8 "%T%\atlantisb-X3.clmb" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 2 & exit /b 1)
"%~d0\NWGET243.EXE" 3 c1c8e9ac706099da73b3657ccc2c44d5f3e42505979e0328923cc28940b90091 "%T%\atlantisb-X3.trx" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 3 & exit /b 1)
"%~d0\NWGET243.EXE" 4 6b517281b2b87a57bb9489c55fd25eec8ea9ec6ef451b1f3da0a3ed0834da112 "%T%\atlantisb-X3.txcb" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 4 & exit /b 1)
"%~d0\NWGET243.EXE" 5 fad06400ee18156a8c93292857c4507e96f604e874198aa3e04c4a84436a3424 "%T%\nwoa4378.cat" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 5 & exit /b 1)
"%~d0\NWGET243.EXE" 6 2b0be1116572ca527123ce596c63fc9e5dd42152cf43cb8e4a965475055f699d "%T%\nwoa4378.inf" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 6 & exit /b 1)
"%~d0\NWGET243.EXE" 7 a6daf75497cfd4d039bfef54f5f28b329920b83e71c510037cfdbea097ab07a2 "%T%\nwoa4378.sys" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 7 & exit /b 1)
"%~d0\NWGET243.EXE" 8 ac4e8367745f428ff68a627da2a660afd0298fc51e88c69a2c34e8129853b446 "%T%\nwoas-bcm4378-board.bin" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 8 & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('P-atlantisb_M-RASP_V-m.txt','P-atlantisb_M-RASP_V-u.txt','atlantisb-X3.clmb','atlantisb-X3.trx','atlantisb-X3.txcb','nwoa4378.cat','nwoa4378.inf','nwoa4378.sys','nwoas-bcm4378-board.bin'); $h=@('5e8c0449664fa47d57f220aa75dd04ad6d5558adf157ddf20f447a9f4f5fb19e','417a6014f4dad80d92f160748f43d2c50314ba67eefc15fa5eace378d82d943e','322fbc347d7ee69d8db0bb8483c9e7e8ac711390b1f4dd951c12c5ec153f4ab8','c1c8e9ac706099da73b3657ccc2c44d5f3e42505979e0328923cc28940b90091','6b517281b2b87a57bb9489c55fd25eec8ea9ec6ef451b1f3da0a3ed0834da112','fad06400ee18156a8c93292857c4507e96f604e874198aa3e04c4a84436a3424','2b0be1116572ca527123ce596c63fc9e5dd42152cf43cb8e4a965475055f699d','a6daf75497cfd4d039bfef54f5f28b329920b83e71c510037cfdbea097ab07a2','ac4e8367745f428ff68a627da2a660afd0298fc51e88c69a2c34e8129853b446'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $env:T $n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); $l=(Get-Item -LiteralPath $f).Length; if($g -eq $h[$i]){ 'S243 READBACK MATCH '+$n[$i]+' '+$l+' '+$g } else { 'S243 READBACK MISMATCH '+$n[$i]+' '+$l+' '+$g; $bad=1 } }; exit $bad" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL readback & exit /b 1)
echo S243 DELIVER COMPLETE
exit /b 0
