@echo off
echo GPUCOV K14b CINEBENCH UI with every D3D10/11 shader forced through the DXBC-to-NIR translator (A/B against the TGSI path)
set D=C:\NWOAS\gpucov
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
> %D%\k14.cmd echo @echo off
>> %D%\k14.cmd echo set APPLE_AGX_D3D_SHADER_NIR=1
>> %D%\k14.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\cb26ui.ps1 -Wait 75
del /q %D%\cb26-done.txt 2>nul
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K14b -Cmd %D%\k14.cmd -Done %D%\cb26-done.txt -Timeout 200
powershell -NoProfile -Command "$d='C:\NWOAS\gpucov'; if(Test-Path $d\cb26-stat.txt){Get-Content $d\cb26-stat.txt | ForEach-Object {'K14b '+$_}}; $t=Join-Path $d 'cb26-umd.log'; if(!(Test-Path $t)){'K14b NO TRACE';exit}; $l=Get-Content $t | ForEach-Object { $_ -replace ' pid=\d+ tid=\d+ session=\d+ qpc=\d+','' }; $s=@($l -match '^measure-d3d11-shader '); $r=@($l -match '^reject-d3d11-shader '); 'K14b SHADERS created='+$s.Count+' rejected='+$r.Count+' seterror='+@($l -match '^reject-seterror').Count+' notarget='+@($l -match '^reject-no-target').Count; $s | ForEach-Object { $v=$_ -split ' '; 'stage='+$v[2]+' path='+$v[3]+' version='+$v[4] } | Group-Object | ForEach-Object { 'K14b KIND '+$_.Count+' '+$_.Name }; $r | Select-Object -First 20 | ForEach-Object { 'K14b REJ '+$_ }; $l | Where-Object { $_ -match '^reject-' -and $_ -notmatch '^reject-cxx|^reject-d3d11-shader' } | Select-Object -First 20 | ForEach-Object { 'K14b R '+$_ }; $s | Select-Object -First 60 | ForEach-Object { 'K14b S '+$_ }; if(Test-Path $d\dxbc){ $f=@(Get-ChildItem $d\dxbc -File); 'K14b DXBC files='+$f.Count+' bytes='+($f | Measure-Object Length -Sum).Sum }"
echo GPUCOV K14b DONE
exit /b 0
