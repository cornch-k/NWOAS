# s258m W17: start the NwoasWlan ETW session (circular 192 MB) to C:\NWOAS\wifitrace\wlan.etl with the
# named WLAN providers that exist on this Windows, every provider the WlanSvc process registered, and kernel
# providers whose name mentions wifi/wlan/wdi/dot11.  Prints which ones were found.
$d='C:\NWOAS\wifitrace'; New-Item -ItemType Directory -Force -Path $d | Out-Null
$b=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime; 'W17 NOW '+(Get-Date).ToString('s')+' UP_MS '+[int64]((Get-Date)-$b).TotalMilliseconds
logman stop NwoasWlan -ets 2>&1 | Out-Null
$etl=Join-Path $d 'wlan.etl'; $pf=Join-Path $d 'prov.txt'
foreach($x in 'wlan.etl','wlan.txt','wlan.csv'){ Remove-Item -LiteralPath (Join-Path $d $x) -Force -ErrorAction SilentlyContinue }
$rg='\{[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\}'; $G=@{}
foreach($p in 'Microsoft-Windows-WLAN-AutoConfig','Microsoft-Windows-NWiFi','Microsoft-Windows-WiFiNetworkManager','Microsoft-Windows-WLAN-Driver','Microsoft-Windows-VWiFi'){
 $q=(logman query providers $p 2>&1) | Out-String
 if($q -match $rg){ $G[$matches[0].ToUpper()]=$p; 'W17 PROV '+$p+' '+$matches[0] } else { 'W17 NOPROV '+$p } }
# WPP control GUIDs from Microsoft Learn "WDI tracing with WDILib" (OS side only; text needs TMFs, so the
# filter in W18 mostly sees their counts; the .etl keeps them for later)
foreach($w in '{21BA7B61-05F8-41F1-9048-C09493DCFE38}=WDI Driver WPP','{0C5A3172-2248-44FD-B9A6-8389CB1DC56A}=wlansvc WPP','{D905AC1D-65E7-4242-99EA-FE66A8355DF8}=WlanAPI MSM WPP','{D905AC1C-65E7-4242-99EA-FE66A8355DF8}=NWifi WPP','{20644520-D1C2-4024-B6F6-311F99AA51ED}=MSMSEC','{ED092A80-0125-4403-92AC-4C06632420F8}=MSMSEC','{253F4CD1-9475-4642-88E0-6790D7A86CDE}=MSMSEC','{5CA18737-22AC-4050-85BC-B8DBB9F7D986}=WifiNetworkManager WPP'){ $z=$w.Split('='); if(-not $G.ContainsKey($z[0])){ $G[$z[0]]=$z[1] } }
foreach($l in @(netsh trace show scenarios 2>&1)){ if(([string]$l) -match '(?i)wlan|wireless|wifi'){ 'W17 SCEN '+([string]$l).Trim() } }
$s=Get-CimInstance Win32_Service -Filter 'Name=''WlanSvc'''; 'W17 WLANSVC pid='+$s.ProcessId+' state='+$s.State
$k=0; if($s.ProcessId){ foreach($l in @(logman query providers -pid $s.ProcessId 2>&1)){ $m=[regex]::Match([string]$l,$rg); if($m.Success -and -not $G.ContainsKey($m.Value.ToUpper())){ $G[$m.Value.ToUpper()]='wlansvc:'+([string]$l).Trim(); $k++ } } }
'W17 WLANSVC_PROVIDERS '+$k
$k=0; $k4=0; foreach($l in @(logman query providers -pid 4 2>&1)){ $t=[string]$l; if($t -match $rg){ $k4++ }; $m=[regex]::Match($t,$rg); if($m.Success -and $t -match 'wifi|wlan|wdi|dot11|wireless|nwf' -and $t -notmatch 'Kernel-WDI|Diagnosis-WDI' -and -not $G.ContainsKey($m.Value.ToUpper())){ $G[$m.Value.ToUpper()]='pid4:'+$t.Trim(); $k++; 'W17 KPROV '+$t.Trim() } }
'W17 KERNEL_PROVIDERS '+$k+' of '+$k4
$G.Keys | ForEach-Object { $_+' 0xffffffffffffffff 0xff' } | Set-Content -LiteralPath $pf -Encoding Ascii
'W17 PROVIDERS_TOTAL '+$G.Count
$o=logman start NwoasWlan -pf $pf -o $etl -mode Circular -max 192 -bs 128 -nb 32 128 -ets 2>&1; 'W17 LOGMAN_RC '+$LASTEXITCODE+' '+(($o | Out-String) -replace '\s+',' ').Trim()
foreach($l in @(logman query NwoasWlan -ets 2>&1 | Select-Object -First 12)){ if(([string]$l).Trim()){ 'W17 Q '+([string]$l).Trim() } }
