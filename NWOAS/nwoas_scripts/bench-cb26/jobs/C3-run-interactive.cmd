@echo off
echo CB26 C3 RUN Cinebench 2026 in the logged-on user's session (multi, then single; min 60 s each)
set "A=C:\NWOAS\cb26\app"
icacls C:\NWOAS\cb26 /grant *S-1-5-32-545:(OI)(CI)M /T >nul
> C:\NWOAS\cb26\run.cmd echo @echo off
>> C:\NWOAS\cb26\run.cmd echo cd /d %A%
>> C:\NWOAS\cb26\run.cmd echo Cinebench.exe g_CinebenchCpuXTest=true g_CinebenchMinimumTestDuration=60 ^> C:\NWOAS\cb26\multi.txt 2^>^&1
>> C:\NWOAS\cb26\run.cmd echo Cinebench.exe g_CinebenchCpu1Test=true g_CinebenchMinimumTestDuration=60 ^> C:\NWOAS\cb26\single.txt 2^>^&1
>> C:\NWOAS\cb26\run.cmd echo echo done^> C:\NWOAS\cb26\done.txt
del /q C:\NWOAS\cb26\multi.txt C:\NWOAS\cb26\single.txt C:\NWOAS\cb26\done.txt 2>nul
powershell -NoProfile -Command "$a=New-ScheduledTaskAction -Execute 'cmd.exe' -Argument '/c C:\NWOAS\cb26\run.cmd'; $p=New-ScheduledTaskPrincipal -GroupId 'S-1-5-32-545' -RunLevel Limited; Register-ScheduledTask -TaskName 'NWOAS-CB26' -Action $a -Principal $p -Force | Out-Null; Start-ScheduledTask -TaskName 'NWOAS-CB26'; $t0=Get-Date; while(-not (Test-Path 'C:\NWOAS\cb26\done.txt') -and ((Get-Date)-$t0).TotalSeconds -lt 900){ Start-Sleep 5 }; 'WAITED '+[int]((Get-Date)-$t0).TotalSeconds+' s'; Unregister-ScheduledTask -TaskName 'NWOAS-CB26' -Confirm:$false; foreach($f in 'multi','single'){ '=== '+$f; if(Test-Path \"C:\NWOAS\cb26\$f.txt\"){ Get-Content \"C:\NWOAS\cb26\$f.txt\" | Select-String -Pattern 'CB |CB:|Score|score|pts|Error|crash|Running' | Select-Object -Last 12 | ForEach-Object { $_.Line } } else { 'NO OUTPUT' } }"
echo CB26 C3 DONE
exit /b 0
