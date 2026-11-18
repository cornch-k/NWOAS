@echo off
echo GPUCOV K11 INTEROP (nwgpuprobe4 interop, user session): D2D/DWrite on D3D11 and D3D11On12, GetDC on a GDI swapchain, D3D11/D3D12 shared textures, keyed mutex, layered windows
set D=C:\NWOAS\gpucov
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
> %D%\k11.cmd echo @echo off
>> %D%\k11.cmd echo %D%\nwgpuprobe4-arm64.exe interop ^> %D%\interop.txt 2^>^&1
>> %D%\k11.cmd echo echo done^> %D%\k11-done.txt
del /q %D%\interop.txt 2>nul
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K11 -Cmd %D%\k11.cmd -Done %D%\k11-done.txt -Timeout 180
powershell -NoProfile -Command "$f='C:\NWOAS\gpucov\interop.txt'; if(!(Test-Path $f)){'K11 NO OUTPUT';exit}; $l=Get-Content $f; 'K11 pass='+@($l -match 'result=PASS').Count+' fail='+@($l -match 'result=FAIL').Count; $l | Where-Object { $_ -match 'result=|hr=0x8|removed_reason=0x[1-9a-f]' } | Select-Object -First 30 | ForEach-Object { 'K11 '+$_ }"
echo GPUCOV K11 DONE
exit /b 0
