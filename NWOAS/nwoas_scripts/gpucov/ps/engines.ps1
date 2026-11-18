# What Task Manager's GPU page is built from: GPU engine counter instances
# (one per process x engine), adapter memory counters, and the WMI controller.
$ErrorActionPreference='SilentlyContinue'
$s = (Get-Counter '\GPU Engine(*)\Utilization Percentage').CounterSamples
$eng = $s | ForEach-Object { if ($_.InstanceName -match 'phys_(\d+)_eng_(\d+)_engtype_(.*)$') { "phys$($matches[1]) eng$($matches[2]) $($matches[3])" } } | Sort-Object -Unique
"ENGINES instances=$(@($s).Count) unique=$(@($eng).Count)"
$eng | ForEach-Object { 'ENGINE ' + $_ }
$busy = $s | Group-Object { if ($_.InstanceName -match 'engtype_(.*)$') { $matches[1] } } | ForEach-Object { 'ENGINE_UTIL ' + $_.Name + ' sum_pct=' + [math]::Round((($_.Group | Measure-Object CookedValue -Sum).Sum), 1) }
$busy
foreach ($c in '\GPU Adapter Memory(*)\Dedicated Usage', '\GPU Adapter Memory(*)\Shared Usage', '\GPU Adapter Memory(*)\Total Committed', '\GPU Local Adapter Memory(*)\Local Usage', '\GPU Non Local Adapter Memory(*)\Non Local Usage') {
  (Get-Counter $c).CounterSamples | ForEach-Object { 'MEMCTR ' + ($c -replace '^\\|\(\*\)', '') + ' ' + ($_.InstanceName -replace '^.*(phys_\d+.*)$', '$1') + ' mib=' + [math]::Round($_.CookedValue / 1MB, 1) }
}
Get-CimInstance Win32_VideoController | ForEach-Object { 'VC "' + $_.Name + '" driver=' + $_.DriverVersion + ' status=' + $_.Status + ' mode="' + $_.VideoModeDescription + '" hz=' + $_.CurrentRefreshRate + ' adapter_ram_mib=' + [int]($_.AdapterRAM / 1MB) }
$k = Get-ChildItem 'HKLM:\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}' | Where-Object { $_.PSChildName -match '^\d{4}$' }
foreach ($x in $k) { $v = Get-ItemProperty $x.PSPath; if ($v.MatchingDeviceId) { 'CLASS ' + $x.PSChildName + ' ' + $v.MatchingDeviceId + ' ' + $v.DriverVersion + ' UMD=' + (($v.UserModeDriverName) -join ',') + ' WoW=' + (($v.UserModeDriverNameWow) -join ',') + ' GL=' + $v.OpenGLDriverName + ' qwMem_mib=' + [int]($v.'HardwareInformation.qwMemorySize' / 1MB) } }
