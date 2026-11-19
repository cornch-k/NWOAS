@echo off
echo S243 DELIVER BEGIN 9 file(s) to C:\NWOAS\drop\wifi-j274-20261119-0816-s258p
set "T=C:\NWOAS\drop\wifi-j274-20261119-0816-s258p"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $p=$env:T; while($p){ if(Test-Path -LiteralPath $p){ $x=Get-Item -LiteralPath $p -Force; if(-not $x.PSIsContainer -or ($x.Attributes -band [IO.FileAttributes]::ReparsePoint)){ throw ('unsafe path component '+$p) } }; $p=Split-Path -Parent $p }; New-Item -ItemType Directory -Force -Path $env:T | Out-Null; 'S243 DIR READY '+$env:T" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL dir & exit /b 1)
"%~d0\NWGET243.EXE" 0 5e8c0449664fa47d57f220aa75dd04ad6d5558adf157ddf20f447a9f4f5fb19e "%T%\P-atlantisb_M-RASP_V-m.txt" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 0 & exit /b 1)
"%~d0\NWGET243.EXE" 1 417a6014f4dad80d92f160748f43d2c50314ba67eefc15fa5eace378d82d943e "%T%\P-atlantisb_M-RASP_V-u.txt" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 1 & exit /b 1)
"%~d0\NWGET243.EXE" 2 ecee6770918bfd6daf3faf67cb8cf549015acd24acf0f3d2a5d5a0d8dea791d5 "%T%\atlantisb-X3.clmb" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 2 & exit /b 1)
"%~d0\NWGET243.EXE" 3 7230e9ea5a3ac10c9ba5ea9093f678ae813b0423bbf1f37f7c38ca12eebb1e06 "%T%\atlantisb-X3.trx" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 3 & exit /b 1)
"%~d0\NWGET243.EXE" 4 61c650d5481c69fe03ec80bad0419d372cd007d4aef1850c2d3f17db45bd48ec "%T%\atlantisb-X3.txcb" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 4 & exit /b 1)
"%~d0\NWGET243.EXE" 5 7aee1f045e4d8fc47f6885b67b6a8970c57a966892cb0c1a7d36b3b0997d8e47 "%T%\nwoa4378.cat" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 5 & exit /b 1)
"%~d0\NWGET243.EXE" 6 72511eaf25bd6eaf09ac081082df63fd5bcc0b1bb18d82adcbd38e092910ae52 "%T%\nwoa4378.inf" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 6 & exit /b 1)
"%~d0\NWGET243.EXE" 7 547149a6e9ae6bf95e6fd24e460baef64ae5170c9f7f87d6719f5d5008a429b4 "%T%\nwoa4378.sys" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 7 & exit /b 1)
"%~d0\NWGET243.EXE" 8 ac4e8367745f428ff68a627da2a660afd0298fc51e88c69a2c34e8129853b446 "%T%\nwoas-bcm4378-board.bin" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 8 & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('P-atlantisb_M-RASP_V-m.txt','P-atlantisb_M-RASP_V-u.txt','atlantisb-X3.clmb','atlantisb-X3.trx','atlantisb-X3.txcb','nwoa4378.cat','nwoa4378.inf','nwoa4378.sys','nwoas-bcm4378-board.bin'); $h=@('5e8c0449664fa47d57f220aa75dd04ad6d5558adf157ddf20f447a9f4f5fb19e','417a6014f4dad80d92f160748f43d2c50314ba67eefc15fa5eace378d82d943e','ecee6770918bfd6daf3faf67cb8cf549015acd24acf0f3d2a5d5a0d8dea791d5','7230e9ea5a3ac10c9ba5ea9093f678ae813b0423bbf1f37f7c38ca12eebb1e06','61c650d5481c69fe03ec80bad0419d372cd007d4aef1850c2d3f17db45bd48ec','7aee1f045e4d8fc47f6885b67b6a8970c57a966892cb0c1a7d36b3b0997d8e47','72511eaf25bd6eaf09ac081082df63fd5bcc0b1bb18d82adcbd38e092910ae52','547149a6e9ae6bf95e6fd24e460baef64ae5170c9f7f87d6719f5d5008a429b4','ac4e8367745f428ff68a627da2a660afd0298fc51e88c69a2c34e8129853b446'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $env:T $n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); $l=(Get-Item -LiteralPath $f).Length; if($g -eq $h[$i]){ 'S243 READBACK MATCH '+$n[$i]+' '+$l+' '+$g } else { 'S243 READBACK MISMATCH '+$n[$i]+' '+$l+' '+$g; $bad=1 } }; exit $bad" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL readback & exit /b 1)
echo S243 DELIVER COMPLETE
exit /b 0
