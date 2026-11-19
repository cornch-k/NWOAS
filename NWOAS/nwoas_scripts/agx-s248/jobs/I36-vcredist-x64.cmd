@echo off
echo S248 I36 VC++ X64 REDIST (P37 prerequisite: the x64 UMD imports VCRUNTIME140/VCRUNTIME140_1/MSVCP140; Microsoft: the X64 Redistributable contains both ARM64 and X64 binaries)
if not exist C:\NWOAS\drop\vcredist\vc_redist.x64.exe (echo S248 I36 NO FILE C:\NWOAS\drop\vcredist\vc_redist.x64.exe & exit /b 1)
C:\NWOAS\drop\vcredist\vc_redist.x64.exe /install /quiet /norestart /log C:\NWOAS\vcredist-x64.log
echo S248 I36 EXIT %ERRORLEVEL% (0 ok, 1638 newer already installed, 3010 restart needed)
powershell -NoProfile -Command "foreach($n in 'vcruntime140.dll','vcruntime140_1.dll','msvcp140.dll'){ $f='C:\Windows\System32\'+$n; if(Test-Path $f){ 'CRT '+$n+' '+(Get-Item $f).VersionInfo.FileVersion } else { 'CRT '+$n+' MISSING' } }"
echo S248 I36 DONE
exit /b 0
