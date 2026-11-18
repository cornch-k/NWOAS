@echo off
echo GPUCOV K14 CINEBENCH UI with D3D10/11 shader receipts and DXBC dump (needs 2761)
set D=C:\NWOAS\gpucov
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
> %D%\k14.cmd echo @echo off
>> %D%\k14.cmd echo set APPLE_AGX_D3D_SHADER_DUMP=1
>> %D%\k14.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\cb26ui.ps1 -Wait 75
del /q %D%\cb26-done.txt 2>nul
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K14 -Cmd %D%\k14.cmd -Done %D%\cb26-done.txt -Timeout 200
powershell -NoProfile -Command "$d='C:\NWOAS\gpucov'; if(Test-Path $d\cb26-stat.txt){Get-Content $d\cb26-stat.txt | ForEach-Object {'K14 '+$_}}; $t=Join-Path $d 'cb26-umd.log'; if(!(Test-Path $t)){'K14 NO TRACE';exit}; $l=Get-Content $t | ForEach-Object { $_ -replace ' pid=\d+ tid=\d+ session=\d+ qpc=\d+','' }; $s=@($l -match '^measure-d3d11-shader '); $r=@($l -match '^reject-d3d11-shader '); 'K14 SHADERS created='+$s.Count+' rejected='+$r.Count+' seterror='+@($l -match '^reject-seterror').Count+' notarget='+@($l -match '^reject-no-target').Count; $s | ForEach-Object { $v=$_ -split ' '; 'stage='+$v[2]+' path='+$v[3]+' version='+$v[4] } | Group-Object | ForEach-Object { 'K14 KIND '+$_.Count+' '+$_.Name }; $r | Select-Object -First 20 | ForEach-Object { 'K14 REJ '+$_ }; $l | Where-Object { $_ -match '^reject-' -and $_ -notmatch '^reject-cxx|^reject-d3d11-shader' } | Select-Object -First 20 | ForEach-Object { 'K14 R '+$_ }; $s | Select-Object -First 60 | ForEach-Object { 'K14 S '+$_ }; if(Test-Path $d\dxbc){ $f=@(Get-ChildItem $d\dxbc -File); 'K14 DXBC files='+$f.Count+' bytes='+($f | Measure-Object Length -Sum).Sum }"
echo GPUCOV K14 DONE
exit /b 0
