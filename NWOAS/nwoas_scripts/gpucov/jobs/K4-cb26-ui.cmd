@echo off
echo GPUCOV K4 CINEBENCH 2026 UI (user session, UMD trace for this process only)
set D=C:\NWOAS\gpucov
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
icacls C:\NWOAS\cb26 /grant *S-1-5-32-545:(OI)(CI)M /T >nul
> %D%\k4.cmd echo @echo off
>> %D%\k4.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\cb26ui.ps1 -Wait 75
del /q %D%\cb26-done.txt 2>nul
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K4 -Cmd %D%\k4.cmd -Done %D%\cb26-done.txt -Timeout 200
powershell -NoProfile -Command "$d='C:\NWOAS\gpucov'; if(Test-Path $d\cb26-stat.txt){Get-Content $d\cb26-stat.txt | ForEach-Object {'K4 '+$_}} else {'K4 NO STAT'}; foreach($f in 'cb26-ui.txt','cb26-ui-err.txt'){ if(Test-Path $d\$f){ Get-Content $d\$f | Select-String -Pattern 'CRITICAL|rror|DX|irect|GPU|OUTOFMEMORY|0x8' | Select-Object -First 15 | ForEach-Object {'K4 '+$f+' '+$_.Line} } }; $t=Join-Path $d 'cb26-umd.log'; if(Test-Path $t){ $l=Get-Content $t; 'K4 TRACE lines='+$l.Count+' bytes='+(Get-Item $t).Length; 'K4 TRACE heap='+@($l -match '^measure-d3d12-heap ').Count+' heap_oom='+@($l -match '^measure-d3d12-heap hr=0x8007000e').Count+' refused='+@($l -match 'heap-refused|reject-').Count; $l | Where-Object { $_ -match '^measure-d3d12-heap' -and $_ -notmatch 'hr=0x00000000' } | Select-Object -First 12 | ForEach-Object {'K4 TR '+$_}; $l | Where-Object { $_ -match '^reject-' } | Select-Object -First 12 | ForEach-Object {'K4 TR '+$_}; if((Get-Item $t).Length -gt 50MB){Remove-Item $t; 'K4 TRACE deleted (over 50 MB)'} } else {'K4 NO TRACE'}"
echo GPUCOV K4 DONE
exit /b 0
