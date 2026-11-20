@echo off
echo GPUCOV K64b CINEBENCH MULTI THEN SINGLE, WINDOW WATCH (no driver change, no input): the same order as bench-cb26 C3n. Every 20 s: how much of the left panel is pure black; a window picture is kept when it is above 0.3 and once at the start of the single test. Pass B: APPLE_AGX_D3D_PIXEL_CENTER=0 for the Cinebench processes (2779 behaviour).
set D=C:\NWOAS\gpucov
if not exist C:\NWOAS\cb26\app\Cinebench.exe (echo K64 no Cinebench & echo GPUCOV K64b DONE & exit /b 0)
> %D%\k64.ps1 echo param($Tag,$Pc)
>> %D%\k64.ps1 echo Add-Type -AssemblyName System.Drawing
>> %D%\k64.ps1 echo Add-Type 'using System;using System.Runtime.InteropServices;public static class NwR{[StructLayout(LayoutKind.Sequential)]public struct R{public int L,T,Rt,B;}[DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out R r);[DllImport("user32.dll")]public static extern bool SetProcessDPIAware();}'
>> %D%\k64.ps1 echo [void][NwR]::SetProcessDPIAware()
>> %D%\k64.ps1 echo $d='C:\NWOAS\gpucov\k64'; if(-not (Test-Path $d)){ New-Item -ItemType Directory $d }; $o=$d+'\k64-'+$Tag+'.txt'; Remove-Item $o -ErrorAction SilentlyContinue; $saved=0
>> %D%\k64.ps1 echo if($Pc -ne ''){ $env:APPLE_AGX_D3D_PIXEL_CENTER=$Pc }
>> %D%\k64.ps1 echo foreach($m in 'CpuXTest','Cpu1Test'){ $p=Start-Process C:\NWOAS\cb26\app\Cinebench.exe -ArgumentList ('g_Cinebench'+$m+'=true'),'g_CinebenchMinimumTestDuration=60' -WorkingDirectory C:\NWOAS\cb26\app -PassThru; $t0=Get-Date
>> %D%\k64.ps1 echo  while($true){ Start-Sleep 20; $s=[int]((Get-Date)-$t0).TotalSeconds; $q=Get-Process -Id $p.Id -ErrorAction SilentlyContinue; if(-not $q){ Add-Content $o ('K64 '+$Tag+' '+$m+' t='+$s+' exited'); break }; if($s -gt 2700){ Add-Content $o ('K64 '+$Tag+' '+$m+' t='+$s+' time limit'); Stop-Process -Id $p.Id -Force; break }
>> %D%\k64.ps1 echo   $r=New-Object NwR+R; if(-not [NwR]::GetWindowRect($q.MainWindowHandle,[ref]$r)){ continue }; $w=$r.Rt-$r.L; $h=$r.B-$r.T; if($w -lt 300){ continue }
>> %D%\k64.ps1 echo   $b=New-Object Drawing.Bitmap $w,$h; $g=[Drawing.Graphics]::FromImage($b); $g.CopyFromScreen($r.L,$r.T,0,0,$b.Size); $n=0; $k=0; $u=@{}; $x1=[int]($w*0.21); $y0=[int]($h*0.12)
>> %D%\k64.ps1 echo   for($y=$y0;$y -lt $h-8;$y+=6){ for($x=8;$x -lt $x1;$x+=6){ $c=$b.GetPixel($x,$y); $n++; if($c.R -lt 12 -and $c.G -lt 12 -and $c.B -lt 12){ $k++ }; $u[($c.R -shr 4)*256+($c.G -shr 4)*16+($c.B -shr 4)]=1 } }
>> %D%\k64.ps1 echo   $f=[math]::Round($k/$n,3); Add-Content $o ('K64 '+$Tag+' '+$m+' t='+$s+' win='+$w+'x'+$h+' panel_black='+$f+' colors='+$u.Count)
>> %D%\k64.ps1 echo   if(($f -gt 0.3 -and $saved -lt 3) -or ($s -lt 45 -and $m -eq 'Cpu1Test')){ $b.Save($d+'\k64-'+$Tag+'-'+$m+'-'+$s+'.png'); if($f -gt 0.3){ $saved++ } }
>> %D%\k64.ps1 echo   $g.Dispose(); $b.Dispose() } }
>> %D%\k64.ps1 echo Set-Content ($d+'\k64-'+$Tag+'-done.txt') 'done'
> %D%\k64run.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\k64.ps1 -Tag B -Pc "0"
del /q %D%\k64\k64-B-done.txt 2>nul
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K64 -Cmd %D%\k64run.cmd -Done %D%\k64\k64-B-done.txt -Timeout 5700
powershell -NoProfile -Command "$l=@(Get-Content ('C:\NWOAS\gpucov\k64\k64-'+'B'+'.txt') -ErrorAction SilentlyContinue); $l | Where-Object { $_ -notmatch 'panel_black=0 ' } | Select-Object -First 60; $v=@($l | Where-Object { $_ -match 'panel_black=([0-9.]+)' } | ForEach-Object { [double]($_ -replace '.*panel_black=([0-9.]+).*','$1') }); 'K64 SUMMARY '+'B'+' samples='+$v.Count+' max_panel_black='+($v | Measure-Object -Maximum).Maximum+' over_0.3='+@($v | Where-Object { $_ -gt 0.3 }).Count; Get-ChildItem C:\NWOAS\gpucov\k64\*.png -ErrorAction SilentlyContinue | ForEach-Object { 'K64 PNG '+$_.Name+' '+$_.Length }"
echo GPUCOV K64b DONE
exit /b 0
