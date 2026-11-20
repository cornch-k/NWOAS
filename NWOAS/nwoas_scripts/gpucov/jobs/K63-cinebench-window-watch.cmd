@echo off
echo GPUCOV K63 CINEBENCH WINDOW WATCH (no driver change): Cinebench single-core for 7 minutes with no input, twice: A as installed, B with APPLE_AGX_D3D_PIXEL_CENTER=0 for that process (2779 behaviour). Every 15 s: how much of the left panel is pure black. A healthy panel is not black.
set D=C:\NWOAS\gpucov
if not exist C:\NWOAS\cb26\app\Cinebench.exe (echo K63 no Cinebench & echo GPUCOV K63 DONE & exit /b 0)
> %D%\k63.ps1 echo param($Tag,$Pc)
>> %D%\k63.ps1 echo Add-Type -AssemblyName System.Drawing
>> %D%\k63.ps1 echo Add-Type 'using System;using System.Runtime.InteropServices;public static class NwR{[StructLayout(LayoutKind.Sequential)]public struct R{public int L,T,Rt,B;}[DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out R r);[DllImport("user32.dll")]public static extern bool SetProcessDPIAware();}'
>> %D%\k63.ps1 echo [void][NwR]::SetProcessDPIAware()
>> %D%\k63.ps1 echo $o='C:\NWOAS\gpucov\k63-'+$Tag+'.txt'; Remove-Item $o -ErrorAction SilentlyContinue
>> %D%\k63.ps1 echo if($Pc -ne ''){ $env:APPLE_AGX_D3D_PIXEL_CENTER=$Pc }
>> %D%\k63.ps1 echo $p=Start-Process C:\NWOAS\cb26\app\Cinebench.exe -ArgumentList 'g_CinebenchCpu1Test=true','g_CinebenchMinimumTestDuration=60' -WorkingDirectory C:\NWOAS\cb26\app -PassThru
>> %D%\k63.ps1 echo for($i=1;$i -le 28;$i++){ Start-Sleep 15; $q=Get-Process -Id $p.Id -ErrorAction SilentlyContinue; if(-not $q){ Add-Content $o ('K63 '+$Tag+' t='+($i*15)+' exited'); break }
>> %D%\k63.ps1 echo  $r=New-Object NwR+R; if(-not [NwR]::GetWindowRect($q.MainWindowHandle,[ref]$r)){ continue }; $w=$r.Rt-$r.L; $h=$r.B-$r.T; if($w -lt 300){ Add-Content $o ('K63 '+$Tag+' t='+($i*15)+' no window'); continue }
>> %D%\k63.ps1 echo  $cw=[int]($w*0.2); $ch=[int]($h*0.8); $b=New-Object Drawing.Bitmap $cw,$ch; $g=[Drawing.Graphics]::FromImage($b); $g.CopyFromScreen($r.L+[int]($w*0.01),$r.T+[int]($h*0.12),0,0,$b.Size); $n=0; $k=0; $u=@{}
>> %D%\k63.ps1 echo  for($y=0;$y -lt $ch;$y+=5){ for($x=0;$x -lt $cw;$x+=5){ $c=$b.GetPixel($x,$y); $n++; if($c.R -lt 12 -and $c.G -lt 12 -and $c.B -lt 12){ $k++ }; $u[($c.R -shr 4)*256+($c.G -shr 4)*16+($c.B -shr 4)]=1 } }
>> %D%\k63.ps1 echo  Add-Content $o ('K63 '+$Tag+' t='+($i*15)+' win='+$w+'x'+$h+' panel_black='+[math]::Round($k/$n,3)+' colors='+$u.Count); $g.Dispose(); $b.Dispose() }
>> %D%\k63.ps1 echo Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
>> %D%\k63.ps1 echo Set-Content ('C:\NWOAS\gpucov\k63-'+$Tag+'-done.txt') 'done'
> %D%\k63a.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\k63.ps1 -Tag A -Pc ""
> %D%\k63b.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\k63.ps1 -Tag B -Pc 0
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K63a -Cmd %D%\k63a.cmd -Done %D%\k63-A-done.txt -Timeout 520
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K63b -Cmd %D%\k63b.cmd -Done %D%\k63-B-done.txt -Timeout 520
type %D%\k63-A.txt
type %D%\k63-B.txt
powershell -NoProfile -Command "foreach($t in 'A','B'){ $l=@(Get-Content ('C:\NWOAS\gpucov\k63-'+$t+'.txt') -ErrorAction SilentlyContinue | Where-Object { $_ -match 'panel_black=([0-9.]+)' } | ForEach-Object { [double]($_ -replace '.*panel_black=([0-9.]+).*','$1') }); 'K63 SUMMARY '+$t+' samples='+$l.Count+' max_panel_black='+($l | Measure-Object -Maximum).Maximum+' over_0.5='+@($l | Where-Object { $_ -gt 0.5 }).Count }"
echo GPUCOV K63 DONE
exit /b 0
