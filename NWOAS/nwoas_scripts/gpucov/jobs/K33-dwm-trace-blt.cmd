@echo off
echo GPUCOV K33 DWM UMD TRACE during the swapchain probe (registry-armed trace for dwm.exe, 2765+), then the KMD rings
set D=C:\NWOAS\gpucov
set T=C:\NWOAS\umdtrace
if not exist %T% mkdir %T%
del /q %T%\*.log 2>nul
icacls %T% /grant *S-1-1-0:(OI)(CI)M >nul
icacls %T% /setintegritylevel (OI)(CI)low >nul
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
reg add "HKLM\SOFTWARE\NWOAS\AgxUmdTrace" /v Dir /t REG_SZ /d %T% /f /reg:64 >nul
reg add "HKLM\SOFTWARE\NWOAS\AgxUmdTrace" /v Process /t REG_SZ /d dwm.exe /f /reg:64 >nul
reg add "HKLM\SOFTWARE\NWOAS\AgxUmdTrace" /v RefusalsOnly /t REG_DWORD /d 1 /f /reg:64 >nul
> %D%\k33.cmd echo @echo off
>> %D%\k33.cmd echo set APPLE_AGX_UMD_TRACE_FILE=%T%\probe.log
>> %D%\k33.cmd echo set APPLE_AGX_UMD_REFUSALS_ONLY=1
>> %D%\k33.cmd echo %D%\nwgpuprobe6-arm64.exe d3d11ui ^> %D%\k33-driver.txt 2^>^&1
>> %D%\k33.cmd echo echo done^> %D%\k33-done.txt
del /q %D%\k33-done.txt %D%\k33-driver.txt 2>nul
powershell -NoProfile -Command "$t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'WAIT explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds; Start-Sleep 6"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K33 -Cmd %D%\k33.cmd -Done %D%\k33-done.txt -Timeout 240
reg query "HKLM\SOFTWARE\NWOAS\AgxUmdTrace" /reg:64
reg delete "HKLM\SOFTWARE\NWOAS\AgxUmdTrace" /f /reg:64 >nul
reg delete "HKLM\SOFTWARE\WOW6432Node\NWOAS" /f >nul 2>nul
dir C:\NWOAS\umdtrace
powershell -NoProfile -Command "Start-Sleep 5; $l=@(Get-Content C:\NWOAS\gpucov\k33-driver.txt -ErrorAction SilentlyContinue); $l | Where-Object { $_ -match '(blt|flip)_.*result=' } | ForEach-Object { 'K33 P '+$_ }; foreach($f in (Get-ChildItem C:\NWOAS\umdtrace\*.log)){ $x=@(Get-Content $f.FullName); 'K33 FILE '+$f.Name+' lines='+$x.Count+' bytes='+$f.Length; $x | Where-Object { $_ -match '^reject-' } | Group-Object { ($_ -split ' ')[0] } | ForEach-Object { 'K33 R '+$f.Name+' '+$_.Name+' x'+$_.Count }; $x | Where-Object { $_ -match '^reject-' } | Select-Object -First 25 | ForEach-Object { 'K33 RL '+$f.Name+' '+$_.Substring(0,[math]::Min(280,$_.Length)) }; $x | Where-Object { $_ -match '^measure-dxgi' } | Group-Object { ($_ -split ' ')[0] } | ForEach-Object { 'K33 M '+$f.Name+' '+$_.Name+' x'+$_.Count+' first: '+$_.Group[0].Substring(0,[math]::Min(220,$_.Group[0].Length)) } }"
echo GPUCOV K33 DONE
exit /b 0
