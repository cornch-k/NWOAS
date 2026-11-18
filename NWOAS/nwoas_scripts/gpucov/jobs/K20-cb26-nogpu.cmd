@echo off
echo GPUCOV K20 CINEBENCH UI with our GPU device disabled (Windows falls back to Microsoft Basic Render/Display), then re-enabled
set D=C:\NWOAS\gpucov
set A=C:\NWOAS\cb26\app
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
icacls C:\NWOAS\cb26 /grant *S-1-5-32-545:(OI)(CI)M /T >nul
echo K20 WARNING: our GPU device is disabled for about 2 minutes; the screen may go dark; Windows may need a restart to get it back
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v G3Armed /t REG_DWORD /d 1 /f >nul
pnputil /disable-device "ACPI\APPL0002\0"
ping -n 21 127.0.0.1 >nul
powershell -NoProfile -Command "Get-PnpDevice -Class Display | ForEach-Object { 'K20 DISPLAY '+$_.InstanceId+' '+$_.Status+' '+$_.FriendlyName }"
> %D%\k20.cmd echo @echo off
>> %D%\k20.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\cb26ui2.ps1 -Wait 75 
del /q %D%\cb26-done.txt %D%\cb26-stat.txt %D%\cb26-umd.log %D%\cb26-wintree.txt 2>nul
powershell -NoProfile -Command "$t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'WAIT explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K20 -Cmd %D%\k20.cmd -Done %D%\cb26-done.txt -Timeout 200
if not exist %D%\cb26-done.txt (echo K20 RETRY & powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K20 -Cmd %D%\k20.cmd -Done %D%\cb26-done.txt -Timeout 200)
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v G3Armed /t REG_DWORD /d 1 /f >nul
pnputil /enable-device "ACPI\APPL0002\0"
ping -n 61 127.0.0.1 >nul
powershell -NoProfile -Command "$d=Get-PnpDevice -InstanceId 'ACPI\APPL0002\0'; 'K20 DEV AFTER ENABLE '+$d.Status+' '+$d.Problem; if($d.Status -ne 'OK'){ 'K20 GPU NOT RESTORED: restart Windows with agx-s248/run-reboot-arm-*.sh' }"
powershell -NoProfile -Command "$d='C:\NWOAS\gpucov'; foreach($f in 'cb26-stat.txt','warp-proxy.log'){ if(Test-Path $d\$f){ Get-Content $d\$f | Select-Object -First 25 | ForEach-Object { 'K20 '+$_ } } }; if(Test-Path $d\cb26-ui.txt){ Get-Content $d\cb26-ui.txt | Select-String -Pattern 'CRITICAL|rawport|Framework|rror' | Select-Object -First 8 | ForEach-Object { 'K20 ui '+$_.Line } }; if(Test-Path $d\cb26-wintree.txt){ $l=Get-Content $d\cb26-wintree.txt; 'K20 WINTREE lines='+$l.Count; $l | Where-Object { $_ -match '^WT t50 |^WINSTAT t50_' } | Select-Object -First 90 | ForEach-Object { 'K20 '+$_ } } else { 'K20 NO WINTREE' }"
echo GPUCOV K20 DONE
exit /b 0
