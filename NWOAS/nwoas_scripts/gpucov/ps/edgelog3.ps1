# edgelog3 (2772) = edgelog2 plus: how many GPU processes Edge started and the
# highest --gpu-recent-crash-count (EDGE3 gpu_launches / max_recent_crash), the
# context-loss / GPU-process-exit / out-of-memory log lines (EDGE3 L ...), the
# environment trace at -EnvLog (default edge-umd.log; K38s uses the Low-integrity
# folder C:\NWOAS\umdtrace-low) with the D3D11 render-target / depth refusals and
# the D3D12 allocation-info receipts (P36). Bounded output.
param([string]$Tag = 'K23', [string]$EnvLog = 'C:\NWOAS\gpucov\edge-umd.log')
$d = 'C:\NWOAS\gpucov'
if (Test-Path "$d\edge2-stat.txt") { Get-Content "$d\edge2-stat.txt" | ForEach-Object { "$Tag " + $_.Substring(0, [math]::Min(1500, $_.Length)) } } else { "$Tag NO STAT" }
$st = @(Get-Content "$d\edge2-stat.txt" -ErrorAction SilentlyContinue)
$launch = @($st | Where-Object { $_ -match '^EDGE4 gpu_cmdline ' })
$crash = @($launch | ForEach-Object { if ($_ -match '--gpu-recent-crash-count=([0-9]+)') { [int]$matches[1] } })
$max = if ($crash.Count) { ($crash | Measure-Object -Maximum).Maximum } else { -1 }
"$Tag EDGE3 gpu_launches=$($launch.Count) max_recent_crash=$max"
$l = @(Get-Content "$d\edge2-chrome.log" -ErrorAction SilentlyContinue)
"$Tag LOG lines=$($l.Count)"
$lost = @($l | Where-Object { $_ -match 'Exiting GPU process|context lost|ContextLost|context_lost|Lost context|GL_OUT_OF_MEMORY|OUT_OF_MEMORY|0x8007000E|0x887A0005|DEVICE_REMOVED|GPU process exited|GPU process crashed|gpu_process_host' })
# Each GPU process logs viz_main_impl.cc once at start; EDGE4 can miss one that lives < 150 ms.
$gpids = @($l | Where-Object { $_ -match 'viz_main_impl\.cc' } | ForEach-Object { if ($_ -match '^\[([0-9]+):') { $matches[1] } } | Sort-Object -Unique)
"$Tag EDGE3 gpu_log_pids=$($gpids.Count) ($($gpids -join ',')) lost_lines=$($lost.Count) oom_lines=$(@($l -match 'GL_OUT_OF_MEMORY').Count)"
$lost | Select-Object -First 20 | ForEach-Object { "$Tag L " + $_.Substring(0, [math]::Min(600, $_.Length)) }
$l | Where-Object { $_ -match 'angle|ANGLE|use-gl|use_gl|GLImplementation|EGL|adapter|LUID|luid|D3D11|D3D12|DXGI|SwiftShader|swiftshader|WARP|Vulkan|vulkan|GrContext|Skia|skia|output_device|gpu_init|GpuInit|feature_info|workaround|disabled' -and $_ -notmatch 'extension_registrar|policy|autofill' } | Select-Object -Unique | Select-Object -First 60 | ForEach-Object { "$Tag G " + $_.Substring(0, [math]::Min(300, $_.Length)) }
$idx = @(for ($i = 0; $i -lt $l.Count; $i++) { if ($l[$i] -match ':FATAL:') { $i } })
"$Tag fatals=$($idx.Count)"
foreach ($i in ($idx | Select-Object -First 2)) {
  $a = [math]::Max(0, $i - 40); $b = [math]::Min($l.Count - 1, $i + 12)
  for ($j = $a; $j -le $b; $j++) { "$Tag C$i " + $l[$j].Substring(0, [math]::Min(300, $l[$j].Length)) }
}
$seen = @(Get-ChildItem C:\NWOAS\umdtrace-seen\*.txt -ErrorAction SilentlyContinue)
"$Tag SEEN files=$($seen.Count)"
$seen | Select-Object -First 40 | ForEach-Object { "$Tag SEEN $($_.Name) " + ((Get-Content $_.FullName -ErrorAction SilentlyContinue) -join ' | ') }
$envlog = $EnvLog
if (Test-Path $envlog) {
  $e = @(Get-Content $envlog -ErrorAction SilentlyContinue)
  "$Tag ENVTRACE path=$envlog lines=$($e.Count) pids=$((@($e | ForEach-Object { if ($_ -match ' pid=([0-9]+) ') { $matches[1] } }) | Sort-Object -Unique) -join ',')"
  "$Tag ENVQ rt_refusals=$(@($e -match '^reject-d3d11-render-target').Count) create_as_oom=$(@($e -match '^reject-create-error-as-oom').Count) d3d12_alloc_fail=$(@($e | Where-Object { $_ -match '^measure-d3d12-allocation-info' -and $_ -match ' ffffffff$' }).Count) msaa_checks=$(@($e -match '^measure-msaa-check').Count)"
  $e | Where-Object { $_ -match '^reject-' } | Group-Object { ($_ -split ' ')[0] } | ForEach-Object { "$Tag ENVR $($_.Name) x$($_.Count)" }
  $e | Where-Object { $_ -match '^reject-' } | Select-Object -First 40 | ForEach-Object { "$Tag ENVRL " + $_.Substring(0, [math]::Min(300, $_.Length)) }
  $e | Where-Object { $_ -match '^measure-(d3d12-allocation-info|d3d12-heap|d3d12-owner|cpu-pool|create-error)' } | Select-Object -Last 30 | ForEach-Object { "$Tag ENVM " + $_.Substring(0, [math]::Min(300, $_.Length)) }
} else { "$Tag ENVTRACE none path=$envlog" }
$files = @(Get-ChildItem C:\NWOAS\umdtrace\*.log -ErrorAction SilentlyContinue) + @(Get-ChildItem C:\Users\*\AppData\Local\Temp\*-*.log -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '^[A-Za-z0-9_.]+\.exe-[0-9]+\.log$' })
"$Tag TRACE files=$($files.Count)"
foreach ($f in $files) {
  $x = @(Get-Content $f.FullName -ErrorAction SilentlyContinue)
  "$Tag TFILE $($f.FullName) lines=$($x.Count) armed=$(@($x -match 'measure-trace-armed').Count) rejects=$(@($x -match '^reject-').Count)"
  if ($f.Name -match '^msedge') {
    $x | Select-Object -First 20 | ForEach-Object { "$Tag THEAD $($f.Name) " + $_.Substring(0, [math]::Min(240, $_.Length)) }
    $x | Where-Object { $_ -match '^reject-' } | Select-Object -First 30 | ForEach-Object { "$Tag TREJ $($f.Name) " + $_.Substring(0, [math]::Min(300, $_.Length)) }
  }
}
