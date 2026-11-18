@echo off
echo GPUCOV K14c CINEBENCH UI with D3D10/11 shader receipts and DXBC+signature dump (2761+), waits for the user session
set D=C:\NWOAS\gpucov
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
> %D%\k14c.cmd echo @echo off
>> %D%\k14c.cmd echo set APPLE_AGX_D3D_SHADER_DUMP=1
>> %D%\k14c.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\cb26ui.ps1 -Wait 75
del /q %D%\cb26-done.txt %D%\cb26-stat.txt %D%\cb26-umd.log 2>nul
powershell -NoProfile -Command "$t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'WAIT explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K14c -Cmd %D%\k14c.cmd -Done %D%\cb26-done.txt -Timeout 200
if not exist %D%\cb26-done.txt (echo K14c RETRY & powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K14c -Cmd %D%\k14c.cmd -Done %D%\cb26-done.txt -Timeout 200)
powershell -NoProfile -Command "$d='C:\NWOAS\gpucov'; if(Test-Path $d\cb26-stat.txt){Get-Content $d\cb26-stat.txt | ForEach-Object {'K14c '+$_}}; $t=Join-Path $d 'cb26-umd.log'; if(!(Test-Path $t)){'K14c NO TRACE';exit}; $l=Get-Content $t | ForEach-Object { $_ -replace ' pid=\d+ tid=\d+ session=\d+ qpc=\d+','' }; $s=@($l -match '^measure-d3d11-shader '); $r=@($l -match '^reject-d3d11-shader '); 'K14c SHADERS created='+$s.Count+' rejected='+$r.Count+' seterror='+@($l -match '^reject-seterror').Count+' notarget='+@($l -match '^reject-no-target').Count; $s | ForEach-Object { $v=$_ -split ' '; 'stage='+$v[2]+' path='+$v[3]+' version='+$v[4] } | Group-Object | ForEach-Object { 'K14c KIND '+$_.Count+' '+$_.Name }; $r | Select-Object -First 20 | ForEach-Object { 'K14c REJ '+$_ }; $l | Where-Object { $_ -match '^reject-' -and $_ -notmatch '^reject-cxx|^reject-d3d11-shader' } | Select-Object -First 20 | ForEach-Object { 'K14c R '+$_ }; $s | Select-Object -First 60 | ForEach-Object { 'K14c S '+$_ }; if(Test-Path $d\dxbc){ $f=@(Get-ChildItem $d\dxbc -File); 'K14c DXBC files='+$f.Count+' bytes='+($f | Measure-Object Length -Sum).Sum }"
echo GPUCOV K14c DONE
exit /b 0
