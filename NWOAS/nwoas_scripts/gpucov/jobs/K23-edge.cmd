@echo off
echo GPUCOV K23 EDGE: time to the first non-white frame, GPU process, Chromium log, crash reports (fresh profile)
set D=C:\NWOAS\gpucov
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
> %D%\k23.cmd echo @echo off
>> %D%\k23.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edge2.ps1 -Watch 60
del /q %D%\edge2-done.txt 2>nul
powershell -NoProfile -Command "$t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'WAIT explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K23 -Cmd %D%\k23.cmd -Done %D%\edge2-done.txt -Timeout 240
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edgelog.ps1 -Tag K23
echo GPUCOV K23 DONE
exit /b 0
