@echo off
echo NWOAS ESP-4-2 COPY BEGIN: C:\NWOAS\drop\esp-4-2 -> ESP \m1n1
set "T=C:\NWOAS\drop\esp-4-2"
mountvol S: /S
if errorlevel 1 (echo NWOAS ESP MOUNT FAIL & exit /b 1)
if not exist S:\m1n1 mkdir S:\m1n1
if not exist "%T%\before" mkdir "%T%\before"
if exist "S:\m1n1\boot.bin" copy /Y "S:\m1n1\boot.bin" "%T%\before\boot.bin" >nul
copy /Y "%T%\boot.bin" "S:\m1n1\boot.bin" >nul
if errorlevel 1 (echo NWOAS ESP COPY FAIL boot.bin & mountvol S: /D & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('boot.bin'); $h=@('0191520648dfd5c18d7099b7bc7c2ad95c3aa6d0c21218dd1fb11843b35b6272'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f='S:\m1n1\'+$n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); if($g -eq $h[$i]){ 'NWOAS ESP READBACK MATCH '+$n[$i]+' '+$g } else { 'NWOAS ESP READBACK MISMATCH '+$n[$i]+' '+$g; $bad=1 } }; exit $bad" 2>&1
set RB=%errorlevel%
dir S:\m1n1
mountvol S: /D
if not "%RB%"=="0" (echo NWOAS ESP-4-2 COPY FAIL readback & exit /b 1)
echo NWOAS ESP-4-2 COPY COMPLETE
exit /b 0
