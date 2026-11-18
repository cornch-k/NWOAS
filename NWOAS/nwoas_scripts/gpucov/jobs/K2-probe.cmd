@echo off
echo GPUCOV K2 PROBE arm64 (all) + x64 (user session, window present)
set D=C:\NWOAS\gpucov
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
> %D%\k2.cmd echo @echo off
>> %D%\k2.cmd echo %D%\nwgpuprobe-arm64.exe all ^> %D%\probe-arm64.txt 2^>^&1
>> %D%\k2.cmd echo %D%\nwgpuprobe-x64.exe dxgi d3d12 d3d11 present ^> %D%\probe-x64.txt 2^>^&1
>> %D%\k2.cmd echo echo done^> %D%\k2-done.txt
del /q %D%\probe-arm64.txt %D%\probe-x64.txt 2>nul
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K2 -Cmd %D%\k2.cmd -Done %D%\k2-done.txt -Timeout 300
powershell -NoProfile -Command "foreach($a in 'arm64','x64'){ $f='C:\NWOAS\gpucov\probe-'+$a+'.txt'; if(!(Test-Path $f)){'K2 '+$a+' NO OUTPUT'; continue}; $l=Get-Content $f; 'K2 '+$a+' lines='+$l.Count; $l | Where-Object { $_ -notmatch ' heap kind=| committed_buffer kind=| buffer mib=' -or $_ -notmatch 'hr=0x00000000' } | Select-Object -First 75 | ForEach-Object { 'K2 '+$a+' '+$_ } }"
echo GPUCOV K2 DONE
exit /b 0
