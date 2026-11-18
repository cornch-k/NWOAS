@echo off
echo GPUCOV K36 EDGE GPU PROCESS MODULES + UMD TRACE FOR EVERY PROCESS (registry Process=*, Edge default switches, --disable-gpu-sandbox): does the GPU process load AppleAgxRenderAdmissionUmd.dll, what it logs before the FATAL
set D=C:\NWOAS\gpucov
set T=C:\NWOAS\umdtrace
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
if not exist %T% mkdir %T%
del /q %T%\*.log 2>nul
for /d %%u in (C:\Users\*) do del /q "%%u\AppData\Local\Temp\*.exe-*.log" 2>nul
icacls %T% /grant *S-1-1-0:(OI)(CI)M >nul
icacls %T% /setintegritylevel (OI)(CI)low >nul
reg add "HKLM\SOFTWARE\NWOAS\AgxUmdTrace" /v Dir /t REG_SZ /d %T% /f /reg:64 >nul
reg add "HKLM\SOFTWARE\NWOAS\AgxUmdTrace" /v Process /t REG_SZ /d * /f /reg:64 >nul
reg add "HKLM\SOFTWARE\NWOAS\AgxUmdTrace" /v RefusalsOnly /t REG_DWORD /d 1 /f /reg:64 >nul
> %D%\k-edge.cmd echo @echo off
>> %D%\k-edge.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edge4.ps1 -Watch 45 -Extra:"--disable-gpu-sandbox"
del /q %D%\edge2-done.txt 2>nul
powershell -NoProfile -Command "$t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'WAIT explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds; Start-Sleep 4"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K36 -Cmd %D%\k-edge.cmd -Done %D%\edge2-done.txt -Timeout 240
reg delete "HKLM\SOFTWARE\NWOAS\AgxUmdTrace" /f /reg:64 >nul
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edgelog.ps1 -Tag K36
echo GPUCOV K36 DONE
exit /b 0
