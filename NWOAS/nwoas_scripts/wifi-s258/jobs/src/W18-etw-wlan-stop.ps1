# s258m W18: stop the NwoasWlan ETW session (W17), convert it to text on the Mac mini and print only
# lines about disconnect / roam decisions, masked (MAC, BSSID, SSID, profile, GUID, IPv4, hex byte runs).
# s258n: also 0x + 12 hex digits (NWiFi/AutoConfig print MACs as 'Peer 0x...' / 'self 0x...') and any
# 12-16 hex digit token right after Peer/self/DisAssoc/Assoc/ReAssoc/BSSID/MAC.
# Strong keywords first, then weak ones; a message seen 3 times (digits ignored) is only counted after that.
$d='C:\NWOAS\wifitrace'; $etl=Join-Path $d 'wlan.etl'; $txt=Join-Path $d 'wlan.txt'; $csv=Join-Path $d 'wlan.csv'
$b=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime; 'W18 NOW '+(Get-Date).ToString('s')+' UP_MS '+[int64]((Get-Date)-$b).TotalMilliseconds
$null=logman stop NwoasWlan -ets 2>&1; 'W18 STOP_RC '+$LASTEXITCODE
if(-not (Test-Path -LiteralPath $etl)){ 'W18 NO_ETL'; exit 1 }; 'W18 ETL_BYTES '+(Get-Item -LiteralPath $etl).Length
$null=netsh trace convert input=$etl output=$txt dump=TXT overwrite=yes 2>&1; $src=$txt
if(-not (Test-Path -LiteralPath $txt)){ 'W18 NETSH_CONVERT_FAILED'; $null=tracerpt $etl -o $csv -of CSV -y 2>&1; $src=$csv }
if(-not (Test-Path -LiteralPath $src)){ 'W18 CONVERT_FAILED'; exit 1 }; 'W18 TEXT_BYTES '+(Get-Item -LiteralPath $src).Length+' FROM '+(Split-Path $src -Leaf)
$ks='(?i)disconn|disassoc|deauth|roam|reason|health|inactiv|idle|reconnect|transition|lost|threshold|better|candidate|blocklist|blacklist|penal|connection ?quality'
$kw='(?i)rssi|signal|link ?quality|linkqual|bss ?list|flush|aging|\bage\b|stale|nlo|timeout|keepalive|scan'
$mr='(?i)\b([0-9a-f]{2}[:-]){5}[0-9a-f]{2}\b'; $hx='(?i)\b[0-9a-f]{12}\b'; $h0='(?i)\b0x[0-9a-f]{12}\b'
$nw='(?i)\b(peer|self|disassoc|assoc|reassoc|bssid|mac)\b(\W{1,4}(?:0x)?)[0-9a-f]{12,16}(?![0-9a-f])'; $gu='(?i)\{?[0-9a-f]{8}-([0-9a-f]{4}-){3}[0-9a-f]{12}\}?'
$ip='\b\d{1,3}(\.\d{1,3}){3}\b'; $sb='(?i)\b([0-9a-f]{2} ){5,}[0-9a-f]{2}\b'
$kv='(?i)\b(\w*ssid\w*|profile\w*|\w*bssid\w*|peer\w*|\w*mac\w*|\w*address\w*|interface ?guid|network ?name|name)(\s*[:=]\s*)(''[^'']*''|"[^"]*"|<[^>]*>|[^\s,;\]\)]+)'
# saved profile names / SSIDs on this machine (read from the profile XML, never printed) are masked literally too
$lit=@(); foreach($f in @(Get-ChildItem -LiteralPath 'C:\ProgramData\Microsoft\Wlansvc\Profiles\Interfaces' -Recurse -Filter *.xml -ErrorAction SilentlyContinue)){ try { $q=[xml](Get-Content -LiteralPath $f.FullName -Raw); $lit+=[string]$q.WLANProfile.name; foreach($y in @($q.WLANProfile.SSIDConfig.SSID)){ $lit+=[string]$y.name; $lit+=[string]$y.hex } } catch {} }
$lit=@($lit | Where-Object { $_ -and $_.Length -ge 3 } | Sort-Object Length -Descending -Unique); 'W18 MASK_LITERALS '+$lit.Count
$tot=0; $S=New-Object Collections.ArrayList; $W=New-Object Collections.ArrayList; $Rep=@{}; $P=@{}; $hs=0; $hw=0
foreach($l in [IO.File]::ReadLines($src)){ $tot++
 $m=[regex]::Match($l,'::\S+ \[([^\]]{1,60})\]'); $pn=$(if($m.Success){$m.Groups[1].Value}else{'other'}); $P[$pn]=1+[int]$P[$pn]
 $strong=$l -match $ks; if(-not $strong -and $l -notmatch $kw){ continue }; if($strong){ $hs++ } else { $hw++ }
 $x=[regex]::Replace($l,$kv,'$1$2<m>'); $x=[regex]::Replace($x,$nw,'$1$2<m>'); $x=$x -replace $h0,'<m>' -replace $mr,'<mac>' -replace $sb,'<hex>' -replace $gu,'<guid>' -replace $hx,'<hex12>' -replace $ip,'<ip>' -replace '\s+',' '
 foreach($v in $lit){ $x=$x -ireplace [regex]::Escape($v),'<ssid>' }
 if($x.Length -gt 400){ $x=$x.Substring(0,400) }
 $nk=($x -replace '^.{0,40}?\d\d:\d\d:\d\d[.,]\d+','') -replace '\d+','#'; $Rep[$nk]=1+[int]$Rep[$nk]; if($Rep[$nk] -gt 3){ continue }
 if($strong){ if($S.Count -lt 3000){ [void]$S.Add($x) } } elseif($W.Count -lt 3000){ [void]$W.Add($x) } }
'W18 LINES total='+$tot+' strong='+$hs+' weak='+$hw+' kept_strong='+$S.Count+' kept_weak='+$W.Count
foreach($k in $P.Keys){ 'W18 PROVLINES '+$k+' '+$P[$k] }
$out=0; $cap=30000
foreach($x in $S){ if($out+$x.Length -gt $cap){ 'W18 CAP_REACHED strong'; break }; $out+=$x.Length+3; 'S '+$x }
foreach($x in $W){ if($out+$x.Length -gt $cap){ 'W18 CAP_REACHED weak'; break }; $out+=$x.Length+3; 'W '+$x }
$top=$Rep.GetEnumerator() | Where-Object { $_.Value -gt 3 } | Sort-Object Value -Descending | Select-Object -First 25
foreach($t in $top){ $s='W18 REPEAT '+$t.Value+' '+$t.Key; if($s.Length -gt 220){ $s=$s.Substring(0,220) }; if($out+$s.Length -gt $cap+4000){ break }; $out+=$s.Length; $s }
'W18 PRINTED_BYTES '+$out
