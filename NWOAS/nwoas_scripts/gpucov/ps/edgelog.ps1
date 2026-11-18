# Prints what an Edge run left (edge2/3/4.ps1): the stat file, the GPU-process
# command lines and modules (EDGE4), every distinct ANGLE/GL/GPU/adapter line,
# the 40 log lines before and 12 after the first two FATALs (all levels), and
# the UMD trace files of a registry-armed run. Bounded output.
param([string]$Tag = 'K23')
$d = 'C:\NWOAS\gpucov'
if (Test-Path "$d\edge2-stat.txt") { Get-Content "$d\edge2-stat.txt" | ForEach-Object { "$Tag " + $_.Substring(0, [math]::Min(1500, $_.Length)) } } else { "$Tag NO STAT" }
$l = @(Get-Content "$d\edge2-chrome.log" -ErrorAction SilentlyContinue)
"$Tag LOG lines=$($l.Count)"
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
$envlog = 'C:\NWOAS\gpucov\edge-umd.log'
if (Test-Path $envlog) {
  $e = @(Get-Content $envlog -ErrorAction SilentlyContinue)
  "$Tag ENVTRACE lines=$($e.Count)"
  $e | Where-Object { $_ -match '^reject-' } | Group-Object { ($_ -split ' ')[0] } | ForEach-Object { "$Tag ENVR $($_.Name) x$($_.Count)" }
  $e | Where-Object { $_ -match '^reject-' } | Select-Object -First 40 | ForEach-Object { "$Tag ENVRL " + $_.Substring(0, [math]::Min(300, $_.Length)) }
  $e | Where-Object { $_ -match '^measure-(msaa|d3d12-heap|d3d12-owner|cpu-pool|create-error|caps)' } | Select-Object -First 30 | ForEach-Object { "$Tag ENVM " + $_.Substring(0, [math]::Min(300, $_.Length)) }
}
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
