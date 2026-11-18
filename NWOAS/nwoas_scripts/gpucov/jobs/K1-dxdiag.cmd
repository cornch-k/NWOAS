@echo off
echo GPUCOV K1 DXDIAG (user session)
set D=C:\NWOAS\gpucov
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
> %D%\k1.cmd echo @echo off
>> %D%\k1.cmd echo start "" /wait dxdiag /t %D%\dxdiag.txt
>> %D%\k1.cmd echo echo done^> %D%\k1-done.txt
del /q %D%\dxdiag.txt 2>nul
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K1 -Cmd %D%\k1.cmd -Done %D%\k1-done.txt -Timeout 240
powershell -NoProfile -Command "$f='C:\NWOAS\gpucov\dxdiag.txt'; $t0=Get-Date; while(!(Test-Path $f) -and ((Get-Date)-$t0).TotalSeconds -lt 60){Start-Sleep 2}; if(!(Test-Path $f)){'K1 NO DXDIAG';exit}; Start-Sleep 3; $l=Get-Content $f; 'K1 LINES '+$l.Count; $l | Select-String -Pattern 'Operating System:|DirectX Version:|Card name|Manufacturer:|Chip type|Display Memory|Dedicated Memory|Shared Memory|Current Mode|Driver Name|Driver File Version|Driver Version|Driver Model|Feature Levels|DDI Version|Hardware Scheduling|Graphics Preemption|Compute Preemption|DXVA|Notes:|Problem' | Select-Object -First 60 | ForEach-Object { 'K1 '+$_.Line.Trim() }"
echo GPUCOV K1 DONE
exit /b 0
