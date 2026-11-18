@echo off
echo GPUCOV K34 EDGE UMD TRACE (registry-armed trace for msedge.exe, Edge with --disable-gpu-sandbox so the GPU process can write it, 2765+): which resource or view the GPU process is refused
set D=C:\NWOAS\gpucov
set T=C:\NWOAS\umdtrace
if not exist %T% mkdir %T%
del /q %T%\*.log 2>nul
icacls %T% /grant *S-1-1-0:(OI)(CI)M >nul
icacls %T% /setintegritylevel (OI)(CI)low >nul
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
reg add "HKLM\SOFTWARE\NWOAS\AgxUmdTrace" /v Dir /t REG_SZ /d %T% /f /reg:64 >nul
reg add "HKLM\SOFTWARE\NWOAS\AgxUmdTrace" /v Process /t REG_SZ /d msedge.exe /f /reg:64 >nul
reg add "HKLM\SOFTWARE\NWOAS\AgxUmdTrace" /v RefusalsOnly /t REG_DWORD /d 1 /f /reg:64 >nul
> %D%\k34.cmd echo @echo off
>> %D%\k34.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edge3.ps1 -Watch 40 -Extra:--disable-gpu-sandbox
del /q %D%\edge2-done.txt 2>nul
powershell -NoProfile -Command "$t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'WAIT explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds; Start-Sleep 6"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K34 -Cmd %D%\k34.cmd -Done %D%\edge2-done.txt -Timeout 240
reg query "HKLM\SOFTWARE\NWOAS\AgxUmdTrace" /reg:64
reg delete "HKLM\SOFTWARE\NWOAS\AgxUmdTrace" /f /reg:64 >nul
reg delete "HKLM\SOFTWARE\WOW6432Node\NWOAS" /f >nul 2>nul
dir C:\NWOAS\umdtrace
powershell -NoProfile -Command "Start-Sleep 5; $l=@(Get-Content C:\NWOAS\gpucov\k34-driver.txt -ErrorAction SilentlyContinue); $l | Where-Object { $_ -match '(blt|flip)_.*result=' } | ForEach-Object { 'K34 P '+$_ }; foreach($f in @(Get-ChildItem C:\NWOAS\umdtrace\*.log) + @(Get-ChildItem C:\Users\*\AppData\Local\Temp\msedge*.log -ErrorAction SilentlyContinue)){ $x=@(Get-Content $f.FullName); 'K34 FILE '+$f.Name+' lines='+$x.Count+' bytes='+$f.Length; $x | Where-Object { $_ -match '^reject-' } | Group-Object { ($_ -split ' ')[0] } | ForEach-Object { 'K34 R '+$f.Name+' '+$_.Name+' x'+$_.Count }; $x | Where-Object { $_ -match '^reject-' } | Select-Object -First 25 | ForEach-Object { 'K34 RL '+$f.Name+' '+$_.Substring(0,[math]::Min(280,$_.Length)) }; $x | Select-Object -First 30 | ForEach-Object { 'K34 HEAD '+$f.Name+' '+$_.Substring(0,[math]::Min(240,$_.Length)) }; $x | Where-Object { $_ -match '^measure-dxgi' } | Group-Object { ($_ -split ' ')[0] } | ForEach-Object { 'K34 M '+$f.Name+' '+$_.Name+' x'+$_.Count+' first: '+$_.Group[0].Substring(0,[math]::Min(220,$_.Group[0].Length)) } }"
echo GPUCOV K34 DONE
exit /b 0
