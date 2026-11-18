# s258m W19 (H2 test): ping the BCM4378 default gateway once per second for N seconds; started detached by
# W19-ping-start.cmd, stopped early by W19-ping-stop.cmd (file ping.stop).  Gateway address is never printed.
param([int]$N=1500)
$d='C:\NWOAS\wifitrace'; New-Item -ItemType Directory -Force -Path $d | Out-Null; $o=Join-Path $d 'ping.txt'; $st=Join-Path $d 'ping.stop'
Remove-Item -LiteralPath $st -Force -ErrorAction SilentlyContinue; $b=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime
function Gw { $a=@(Get-NetAdapter -ErrorAction SilentlyContinue | Where-Object { $_.InterfaceDescription -like '*BCM4378*' }); if($a.Count){ $r=@(Get-NetRoute -InterfaceIndex $a[0].ifIndex -DestinationPrefix '0.0.0.0/0' -ErrorAction SilentlyContinue); if($r.Count){ return $r[0].NextHop } }; return $null }
$g=Gw; $p=New-Object Net.NetworkInformation.Ping; $ok=0; $bad=0; $mx=0; $prev=''; $t0=Get-Date
Set-Content -LiteralPath $o -Value ('W19 START t_ms='+[int64]($t0-$b).TotalMilliseconds+' gw='+$(if($g){'found'}else{'none'})+' n='+$N) -Encoding Ascii
for($i=0; $i -lt $N -and -not (Test-Path -LiteralPath $st); $i++){
 $a=Get-Date; $t=[int64]($a-$b).TotalMilliseconds; if(-not $g){ $g=Gw }
 $s='NOGW'; $rt=-1; if($g){ try { $r=$p.Send($g,800); $s=[string]$r.Status; $rt=[int]$r.RoundtripTime } catch { $s='EXC' } }
 if($s -eq 'Success'){ $ok++; if($rt -gt $mx){ $mx=$rt } } else { $bad++ }
 if($s -ne $prev){ Add-Content -LiteralPath $o -Value ('W19 CHG t_ms='+$t+' '+$s+' rtt='+$rt) -Encoding Ascii; $prev=$s }
 if(($i % 60) -eq 59){ Add-Content -LiteralPath $o -Value ('W19 BEAT t_ms='+$t+' ok='+$ok+' bad='+$bad+' maxrtt='+$mx) -Encoding Ascii }
 $w=1000-[int]((Get-Date)-$a).TotalMilliseconds; if($w -gt 0){ Start-Sleep -Milliseconds $w } }
Add-Content -LiteralPath $o -Value ('W19 END t_ms='+[int64]((Get-Date)-$b).TotalMilliseconds+' ok='+$ok+' bad='+$bad+' maxrtt='+$mx) -Encoding Ascii
