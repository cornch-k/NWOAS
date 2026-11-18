@echo off
echo GPUCOV K23b EDGE --disable-direct-composition (control for the skia_output_surface CHECK): time to the first non-white frame, GPU process, Chromium log, crash reports (fresh profile)
set D=C:\NWOAS\gpucov
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
> %D%\k23b.cmd echo @echo off
>> %D%\k23b.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edge3.ps1 -Watch 60 -Extra:--disable-direct-composition
del /q %D%\edge2-done.txt 2>nul
powershell -NoProfile -Command "$t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'WAIT explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K23b -Cmd %D%\k23b.cmd -Done %D%\edge2-done.txt -Timeout 240
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edgelog.ps1 -Tag K23b
echo GPUCOV K23b DONE
exit /b 0
