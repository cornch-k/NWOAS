@echo off
echo GPUCOV K68 CINEBENCH COVER AND UNCOVER (no driver change, no input): a white window covers part of the Cinebench window for 3 s, 5 places, Cinebench idle and then during the single test. changed = share of the uncovered place that differs from before (near 0 = redrawn correctly).
set D=C:\NWOAS\gpucov
> %D%\k68.ps1 echo Add-Type -AssemblyName System.Drawing,System.Windows.Forms
>> %D%\k68.ps1 echo Add-Type 'using System;using System.Runtime.InteropServices;public static class NwR{[StructLayout(LayoutKind.Sequential)]public struct R{public int L,T,Rt,B;}[DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out R r);[DllImport("user32.dll")]public static extern bool SetProcessDPIAware();}'
>> %D%\k68.ps1 echo [void][NwR]::SetProcessDPIAware()
>> %D%\k68.ps1 echo $d='C:\NWOAS\gpucov\k68'; if(-not (Test-Path $d)){ New-Item -ItemType Directory $d }; $o=$d+'\k68.txt'; Remove-Item $o -ErrorAction SilentlyContinue; $sv=0
>> %D%\k68.ps1 echo function Cap($h){ $r=New-Object NwR+R; [void][NwR]::GetWindowRect($h,[ref]$r); $b=New-Object Drawing.Bitmap ($r.Rt-$r.L),($r.B-$r.T); $g=[Drawing.Graphics]::FromImage($b); $g.CopyFromScreen($r.L,$r.T,0,0,$b.Size); $g.Dispose(); @($b,$r) }
>> %D%\k68.ps1 echo function Run($arg,$ph,$wait){ $e='C:\NWOAS\cb26\app'; if($arg){ $p=Start-Process ($e+'\Cinebench.exe') -ArgumentList $arg -WorkingDirectory $e -PassThru } else { $p=Start-Process ($e+'\Cinebench.exe') -WorkingDirectory $e -PassThru }; Start-Sleep $wait; $q=Get-Process -Id $p.Id -ErrorAction SilentlyContinue; if(-not $q){ return }
>> %D%\k68.ps1 echo  foreach($z in @(@(0.01,0.06,0.2,0.4),@(0.01,0.5,0.2,0.4),@(0.15,0.6,0.3,0.3),@(0.3,0.2,0.5,0.6),@(0.01,0.06,0.2,0.4))){ $c=Cap $q.MainWindowHandle; $a=$c[0]; $r=$c[1]; $w=$a.Width; $h=$a.Height; $x0=[int]($w*$z[0]); $y0=[int]($h*$z[1]); $rw=[int]($w*$z[2]); $rh=[int]($h*$z[3])
>> %D%\k68.ps1 echo   $f=New-Object Windows.Forms.Form; $f.FormBorderStyle='None'; $f.StartPosition='Manual'; $f.TopMost=$true; $f.BackColor='White'; $f.Bounds=New-Object Drawing.Rectangle ($r.L+$x0),($r.T+$y0),$rw,$rh; $f.Show(); for($i=0;$i -lt 30;$i++){ [Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 100 }; $f.Close(); $f.Dispose(); for($i=0;$i -lt 25;$i++){ [Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 100 }
>> %D%\k68.ps1 echo   $b=(Cap $q.MainWindowHandle)[0]; $n=0; $df=0; for($y=$y0;$y -lt $y0+$rh;$y+=5){ for($x=$x0;$x -lt $x0+$rw;$x+=5){ $p1=$a.GetPixel($x,$y); $p2=$b.GetPixel($x,$y); $n++; if([math]::Abs($p1.R-$p2.R)+[math]::Abs($p1.G-$p2.G)+[math]::Abs($p1.B-$p2.B) -gt 60){ $df++ } } }
>> %D%\k68.ps1 echo   $fr=[math]::Round($df/$n,3); Add-Content $o ('K68 '+$ph+' rect='+$x0+','+$y0+','+$rw+'x'+$rh+' changed='+$fr); if($fr -gt 0.2 -and $sv -lt 4){ $a.Save($d+'\k68-'+$ph+'-'+$sv+'-before.png'); $b.Save($d+'\k68-'+$ph+'-'+$sv+'-after.png'); $sv++ }; $a.Dispose(); $b.Dispose() }
>> %D%\k68.ps1 echo  Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; Start-Sleep 3 }
>> %D%\k68.ps1 echo Run $null 'idle' 45
>> %D%\k68.ps1 echo Run @('g_CinebenchCpu1Test=true','g_CinebenchMinimumTestDuration=60') 'single' 90
>> %D%\k68.ps1 echo Set-Content ($d+'\k68-done.txt') 'done'
> %D%\k68run.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\k68.ps1
del /q %D%\k68\k68-done.txt 2>nul
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K68 -Cmd %D%\k68run.cmd -Done %D%\k68\k68-done.txt -Timeout 420
type %D%\k68\k68.txt
powershell -NoProfile -Command "$v=@(Get-Content C:\NWOAS\gpucov\k68\k68.txt -ErrorAction SilentlyContinue | Where-Object { $_ -match 'changed=([0-9.]+)' } | ForEach-Object { [double]($_ -replace '.*changed=([0-9.]+).*','$1') }); 'K68 SUMMARY places='+$v.Count+' max_changed='+($v | Measure-Object -Maximum).Maximum+' over_0.2='+@($v | Where-Object { $_ -gt 0.2 }).Count; 'K68 PNG count='+@(Get-ChildItem C:\NWOAS\gpucov\k68\*.png -ErrorAction SilentlyContinue).Count"
echo GPUCOV K68 DONE
exit /b 0
