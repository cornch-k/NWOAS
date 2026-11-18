@echo off
echo GPUCOV K5 EDGE WebGL/WebGPU/decode (user session, own profile)
set D=C:\NWOAS\gpucov
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
> %D%\k5.cmd echo @echo off
>> %D%\k5.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edge.ps1
del /q %D%\edge-done.txt 2>nul
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K5 -Cmd %D%\k5.cmd -Done %D%\edge-done.txt -Timeout 240
powershell -NoProfile -Command "$f='C:\NWOAS\gpucov\edge-stat.txt'; if(Test-Path $f){Get-Content $f | Select-Object -First 40 | ForEach-Object {'K5 '+$_}} else {'K5 NO STAT'}"
echo GPUCOV K5 DONE
exit /b 0
