@echo off
echo GPUCOV K4d CINEBENCH UI with the capability trace (APPLE_AGX_UMD_TRACE_CAPS=1, needs 2760; this process only)
set D=C:\NWOAS\gpucov
> %D%\k4d.cmd echo @echo off
>> %D%\k4d.cmd echo set APPLE_AGX_UMD_TRACE_CAPS=1
>> %D%\k4d.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\cb26ui.ps1 -Wait 75
del /q %D%\cb26-done.txt 2>nul
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K4d -Cmd %D%\k4d.cmd -Done %D%\cb26-done.txt -Timeout 200
powershell -NoProfile -Command "$d='C:\NWOAS\gpucov'; if(Test-Path $d\cb26-stat.txt){Get-Content $d\cb26-stat.txt | ForEach-Object {'K4d '+$_}}; $t=Join-Path $d 'cb26-umd.log'; if(!(Test-Path $t)){'K4d NO TRACE';exit}; $l=Get-Content $t; Copy-Item $t (Join-Path $d 'cb26-caps.log') -Force; $c=@($l -match '^measure-caps-'); 'K4d TRACE lines='+$l.Count+' caps='+$c.Count+' d3d12='+@($c -match '^measure-caps-d3d12 ').Count+' formats='+@($c -match '^measure-caps-d3d12-format').Count+' msaa='+@($c -match '^measure-caps-d3d12-msaa').Count+' d3d11='+@($c -match '^measure-caps-d3d11').Count; $c | Where-Object { $_ -notmatch '^measure-caps-d3d12-format|^measure-caps-d3d12-msaa' } | ForEach-Object { $_ -replace ' pid=\d+ tid=\d+ session=\d+ qpc=\d+','' } | Select-Object -First 60 | ForEach-Object {'K4d C '+$_}; 'K4d FORMATS '+(($c -match '^measure-caps-d3d12-format' | ForEach-Object { ($_ -split ' ')[-2..-1] -join ':' } | Sort-Object -Unique) -join ' ')"
echo GPUCOV K4d DONE
exit /b 0
