@echo off
echo NWOAS ESP-4-1 COPY BEGIN: C:\NWOAS\drop\esp-4-1 -> ESP \m1n1
set "T=C:\NWOAS\drop\esp-4-1"
mountvol S: /S
if errorlevel 1 (echo NWOAS ESP MOUNT FAIL & exit /b 1)
if not exist S:\m1n1 mkdir S:\m1n1
if not exist "%T%\before" mkdir "%T%\before"
if exist "S:\m1n1\boot.bin" copy /Y "S:\m1n1\boot.bin" "%T%\before\boot.bin" >nul
copy /Y "%T%\boot.bin" "S:\m1n1\boot.bin" >nul
if errorlevel 1 (echo NWOAS ESP COPY FAIL boot.bin & mountvol S: /D & exit /b 1)
if exist "S:\m1n1\cleanbp-local.sh" copy /Y "S:\m1n1\cleanbp-local.sh" "%T%\before\cleanbp-local.sh" >nul
copy /Y "%T%\cleanbp-local.sh" "S:\m1n1\cleanbp-local.sh" >nul
if errorlevel 1 (echo NWOAS ESP COPY FAIL cleanbp-local.sh & mountvol S: /D & exit /b 1)
if exist "S:\m1n1\go1.sh" copy /Y "S:\m1n1\go1.sh" "%T%\before\go1.sh" >nul
copy /Y "%T%\go1.sh" "S:\m1n1\go1.sh" >nul
if errorlevel 1 (echo NWOAS ESP COPY FAIL go1.sh & mountvol S: /D & exit /b 1)
if exist "S:\m1n1\nwoas-payload.bin" copy /Y "S:\m1n1\nwoas-payload.bin" "%T%\before\nwoas-payload.bin" >nul
copy /Y "%T%\nwoas-payload.bin" "S:\m1n1\nwoas-payload.bin" >nul
if errorlevel 1 (echo NWOAS ESP COPY FAIL nwoas-payload.bin & mountvol S: /D & exit /b 1)
if exist "S:\m1n1\nwoas-s264g.bin" copy /Y "S:\m1n1\nwoas-s264g.bin" "%T%\before\nwoas-s264g.bin" >nul
copy /Y "%T%\nwoas-s264g.bin" "S:\m1n1\nwoas-s264g.bin" >nul
if errorlevel 1 (echo NWOAS ESP COPY FAIL nwoas-s264g.bin & mountvol S: /D & exit /b 1)
if exist "S:\m1n1\stage1.bin" copy /Y "S:\m1n1\stage1.bin" "%T%\before\stage1.bin" >nul
copy /Y "%T%\stage1.bin" "S:\m1n1\stage1.bin" >nul
if errorlevel 1 (echo NWOAS ESP COPY FAIL stage1.bin & mountvol S: /D & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('boot.bin','cleanbp-local.sh','go1.sh','nwoas-payload.bin','nwoas-s264g.bin','stage1.bin'); $h=@('80a13ca257fcf1d04676e08355b78ffec57cf82fbbbf3b3395f3524daa187e48','d8756b7b00fe4aa7b2e4fd922a6727c87a1df1cdb32acab56b374d9043883327','0adddcf3151fc5fc29e887b41fe0ed23c582303841f33373f8e6cd9810276909','26428b579ba117ae3b26c8d914461cc125b07140de1c1bf3393ff1a62fd0ab92','0191520648dfd5c18d7099b7bc7c2ad95c3aa6d0c21218dd1fb11843b35b6272','8879def896f3577da90ec3d87caef3a549d2b4a0d9b7d6d3ee87b88ee4914778'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f='S:\m1n1\'+$n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); if($g -eq $h[$i]){ 'NWOAS ESP READBACK MATCH '+$n[$i]+' '+$g } else { 'NWOAS ESP READBACK MISMATCH '+$n[$i]+' '+$g; $bad=1 } }; exit $bad" 2>&1
set RB=%errorlevel%
dir S:\m1n1
mountvol S: /D
if not "%RB%"=="0" (echo NWOAS ESP-4-1 COPY FAIL readback & exit /b 1)
echo NWOAS ESP-4-1 COPY COMPLETE
exit /b 0
