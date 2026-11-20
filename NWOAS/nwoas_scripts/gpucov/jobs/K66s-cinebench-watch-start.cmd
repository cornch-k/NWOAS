@echo off
echo GPUCOV K66s START THE CINEBENCH WINDOW WATCHER (no driver change): starts a watcher in the user session and returns at once. It waits for a Cinebench window (bench-cb26 C3n starts it), then every 20 s notes how much of the left panel is pure black, keeps a picture when it is above 0.3 (at most 3) and one normal picture, and stops when C:\NWOAS\cb26\done.txt appears. Read it afterwards with K66r.
set D=C:\NWOAS\gpucov
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
> %D%\k66.ps1 echo Add-Type -AssemblyName System.Drawing
>> %D%\k66.ps1 echo Add-Type 'using System;using System.Runtime.InteropServices;public static class NwR{[StructLayout(LayoutKind.Sequential)]public struct R{public int L,T,Rt,B;}[DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out R r);[DllImport("user32.dll")]public static extern bool SetProcessDPIAware();}'
>> %D%\k66.ps1 echo [void][NwR]::SetProcessDPIAware()
>> %D%\k66.ps1 echo $d='C:\NWOAS\gpucov\k66'; if(-not (Test-Path $d)){ New-Item -ItemType Directory $d }; $o=$d+'\k66.txt'; Remove-Item $o -ErrorAction SilentlyContinue; $saved=0; $good=0; $seen=0; $gone=0; $t0=Get-Date
>> %D%\k66.ps1 echo while($true){ Start-Sleep 20; $s=[int]((Get-Date)-$t0).TotalSeconds; if($s -gt 4200){ Add-Content $o ('K66 t='+$s+' time limit'); break }
>> %D%\k66.ps1 echo  $q=Get-Process Cinebench -ErrorAction SilentlyContinue ^| Where-Object { $_.MainWindowHandle -ne 0 } ^| Select-Object -First 1
>> %D%\k66.ps1 echo  if(-not $q){ if($seen){ $gone++; if((Test-Path C:\NWOAS\cb26\done.txt) -or $gone -gt 6){ Add-Content $o ('K66 t='+$s+' cinebench finished'); break } }; continue }; $seen=1; $gone=0
>> %D%\k66.ps1 echo  $r=New-Object NwR+R; if(-not [NwR]::GetWindowRect($q.MainWindowHandle,[ref]$r)){ continue }; $w=$r.Rt-$r.L; $h=$r.B-$r.T; if($w -lt 300){ continue }
>> %D%\k66.ps1 echo  $b=New-Object Drawing.Bitmap $w,$h; $g=[Drawing.Graphics]::FromImage($b); $g.CopyFromScreen($r.L,$r.T,0,0,$b.Size); $n=0; $k=0; $u=@{}; $x1=[int]($w*0.21); $y0=[int]($h*0.12)
>> %D%\k66.ps1 echo  for($y=$y0;$y -lt $h-8;$y+=6){ for($x=8;$x -lt $x1;$x+=6){ $c=$b.GetPixel($x,$y); $n++; if($c.R -lt 12 -and $c.G -lt 12 -and $c.B -lt 12){ $k++ }; $u[($c.R -shr 4)*256+($c.G -shr 4)*16+($c.B -shr 4)]=1 } }
>> %D%\k66.ps1 echo  $f=[math]::Round($k/$n,3); Add-Content $o ('K66 t='+$s+' pid='+$q.Id+' win='+$w+'x'+$h+' panel_black='+$f+' colors='+$u.Count)
>> %D%\k66.ps1 echo  if($f -gt 0.3 -and $saved -lt 3){ $b.Save($d+'\k66-bad-'+$s+'.png'); $saved++ } elseif($f -le 0.3 -and $good -lt 1 -and $s -gt 120){ $b.Save($d+'\k66-good-'+$s+'.png'); $good++ }
>> %D%\k66.ps1 echo  $g.Dispose(); $b.Dispose() }
>> %D%\k66.ps1 echo Set-Content ($d+'\k66-done.txt') 'done'
> %D%\k66run.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\k66.ps1
del /q %D%\k66\k66-done.txt %D%\k66\k66.txt 2>nul
powershell -NoProfile -Command "$a=New-ScheduledTaskAction -Execute 'cmd.exe' -Argument '/c C:\NWOAS\gpucov\k66run.cmd'; $p=New-ScheduledTaskPrincipal -GroupId 'S-1-5-32-545' -RunLevel Limited; Register-ScheduledTask -TaskName NWOAS-K66 -Action $a -Principal $p -Force | Out-Null; Start-ScheduledTask -TaskName NWOAS-K66; Start-Sleep 5; 'K66s task state='+(Get-ScheduledTask -TaskName NWOAS-K66).State+' ps1='+(Test-Path C:\NWOAS\gpucov\k66.ps1)"
echo GPUCOV K66s DONE
exit /b 0
