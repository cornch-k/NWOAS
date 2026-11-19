@echo off
echo S258 W19 PING START (s276 copy, s258n: gateway of the Up BCM4378 adapter, looked up every 5 s until found), once per second, 2400 s, detached
if not exist "C:\NWOAS\drop\wifi-j274-20261120-0213-s276fw\W19-ping.ps1" (echo S258 W19 FAIL script missing & exit /b 1)
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$c='powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File C:\NWOAS\drop\wifi-j274-20261120-0213-s276fw\W19-ping.ps1 -N 2400'; $r=Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{CommandLine=$c}; 'W19 CREATE rc='+$r.ReturnValue+' pid='+$r.ProcessId" 2>&1
ping -n 6 127.0.0.1 >nul
type C:\NWOAS\wifitrace\ping.txt 2>nul
echo S258 W19 START DONE
exit /b 0
