# s258m/s258n W19 (H2 test): ping the BCM4378 default gateway once per second for N seconds; started detached by
# W19-ping-start.cmd, stopped early by W19-ping-stop.cmd (file ping.stop).  Gateway address is never printed.
param([int]$N=1500)
$d='C:\NWOAS\wifitrace'; New-Item -ItemType Directory -Force -Path $d | Out-Null; $o=Join-Path $d 'ping.txt'; $st=Join-Path $d 'ping.stop'
Remove-Item -LiteralPath $st -Force -ErrorAction SilentlyContinue; $b=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime
# s258n: the s258m version took the first adapter with that description, which can be one of the
# 'Not Present' phantom NICs (no route), so it never found a gateway (s258m H2: NOGW for 1215 s).
# Now: every BCM4378 adapter that is Up and has an IPv4 default route; looked up every 5 s until found,
# and again every 30 s after that (W19 GW lines: found / lost / changed, never the address).
function Gw { foreach($a in @(Get-NetAdapter -ErrorAction SilentlyContinue | Where-Object { $_.InterfaceDescription -like '*BCM4378*' -and $_.Status -eq 'Up' })){ $r=@(Get-NetRoute -InterfaceIndex $a.ifIndex -AddressFamily IPv4 -DestinationPrefix '0.0.0.0/0' -ErrorAction SilentlyContinue | Where-Object { $_.NextHop -and $_.NextHop -ne '0.0.0.0' } | Sort-Object RouteMetric); if($r.Count){ return [string]$r[0].NextHop } }; return $null }
$g=Gw; $p=New-Object Net.NetworkInformation.Ping; $ok=0; $bad=0; $mx=0; $prev=''; $t0=Get-Date; $lk=$t0
Set-Content -LiteralPath $o -Value ('W19 START t_ms='+[int64]($t0-$b).TotalMilliseconds+' gw='+$(if($g){'found'}else{'none'})+' n='+$N) -Encoding Ascii
for($i=0; $i -lt $N -and -not (Test-Path -LiteralPath $st); $i++){
 $a=Get-Date; $t=[int64]($a-$b).TotalMilliseconds
 if(((-not $g) -and ($a-$lk).TotalSeconds -ge 5) -or ($g -and ($a-$lk).TotalSeconds -ge 30)){ $lk=$a; $n2=Gw
  if($n2 -ne $g){ $w2=$(if(-not $g){'found'}elseif(-not $n2){'lost'}else{'changed'}); Add-Content -LiteralPath $o -Value ('W19 GW '+$w2+' t_ms='+$t) -Encoding Ascii; if($n2){ $g=$n2 } } }
 $s='NOGW'; $rt=-1; if($g){ try { $r=$p.Send($g,800); $s=[string]$r.Status; $rt=[int]$r.RoundtripTime } catch { $s='EXC' } }
 if($s -eq 'Success'){ $ok++; if($rt -gt $mx){ $mx=$rt } } else { $bad++ }
 if($s -ne $prev){ Add-Content -LiteralPath $o -Value ('W19 CHG t_ms='+$t+' '+$s+' rtt='+$rt) -Encoding Ascii; $prev=$s }
 if(($i % 60) -eq 59){ Add-Content -LiteralPath $o -Value ('W19 BEAT t_ms='+$t+' ok='+$ok+' bad='+$bad+' maxrtt='+$mx) -Encoding Ascii }
 $w=1000-[int]((Get-Date)-$a).TotalMilliseconds; if($w -gt 0){ Start-Sleep -Milliseconds $w } }
Add-Content -LiteralPath $o -Value ('W19 END t_ms='+[int64]((Get-Date)-$b).TotalMilliseconds+' ok='+$ok+' bad='+$bad+' maxrtt='+$mx) -Encoding Ascii
