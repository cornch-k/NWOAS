@echo off
echo GPUCOV K36b EDGE with the registry trace for every process and the probe breadcrumbs (C:\NWOAS\umdtrace-seen): where the registry trace stops
set D=C:\NWOAS\gpucov
set T=C:\NWOAS\umdtrace
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
del /q %D%\edge-umd.log 2>nul
if not exist %T% mkdir %T%
if not exist C:\NWOAS\umdtrace-seen mkdir C:\NWOAS\umdtrace-seen
del /q %T%\*.log C:\NWOAS\umdtrace-seen\*.txt 2>nul
icacls %T% /grant *S-1-1-0:(OI)(CI)M >nul
icacls C:\NWOAS\umdtrace-seen /grant *S-1-1-0:(OI)(CI)M >nul
reg add "HKLM\SOFTWARE\NWOAS\AgxUmdTrace" /v Dir /t REG_SZ /d %T% /f /reg:64 >nul
reg add "HKLM\SOFTWARE\NWOAS\AgxUmdTrace" /v Process /t REG_SZ /d * /f /reg:64 >nul
reg add "HKLM\SOFTWARE\NWOAS\AgxUmdTrace" /v RefusalsOnly /t REG_DWORD /d 1 /f /reg:64 >nul
> %D%\k-edge.cmd echo @echo off
>> %D%\k-edge.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edge4.ps1 -Watch 45 -Extra:""
del /q %D%\edge2-done.txt 2>nul
powershell -NoProfile -Command "$t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'WAIT explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds; Start-Sleep 4"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K36b -Cmd %D%\k-edge.cmd -Done %D%\edge2-done.txt -Timeout 240
reg delete "HKLM\SOFTWARE\NWOAS\AgxUmdTrace" /f /reg:64 >nul
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edgelog2.ps1 -Tag K36b
rmdir /s /q C:\NWOAS\umdtrace-seen 2>nul
echo GPUCOV K36b DONE
exit /b 0
