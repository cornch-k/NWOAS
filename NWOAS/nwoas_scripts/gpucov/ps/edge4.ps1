# edge4 = edge3 plus a module list of each GPU process while it is alive and its full command line
# (EDGE4 gpu_mod / gpu_cmdline lines). edge3 = edge2 plus -Extra (additional Edge switches). User session: Edge with a fresh profile and Chromium logging. Measures the
# time until the window stops being plain white, then captures edge://gpu
# (headless dump), the GPU-related log lines and the crash reports.
# Outputs: C:\NWOAS\gpucov\edge2-stat.txt, edge2-chrome.log, edge2-gpu.html
param([int]$Watch = 60, [string]$Url = 'file:///C:/NWOAS/gpucov/webgl.html?run=10', [string]$Extra = '')
$d = 'C:\NWOAS\gpucov'; $o = "$d\edge2-stat.txt"; $prof = "$d\edge-prof2"
Add-Type -AssemblyName System.Drawing
if (-not ('NwCap' -as [type])) {
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class NwCap {
  [StructLayout(LayoutKind.Sequential)] public struct R { public int L, T, Rt, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out R r);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
'@
}
[void][NwCap]::SetProcessDPIAware()
function WhiteFraction([IntPtr]$h) {
  $r = New-Object NwCap+R
  if (-not [NwCap]::GetWindowRect($h, [ref]$r)) { return -1 }
  $w = $r.Rt - $r.L; $ht = $r.B - $r.T
  if ($w -lt 40 -or $ht -lt 40) { return -1 }
  $x0 = $r.L + [int]($w * 0.1); $y0 = $r.T + [int]($ht * 0.25); $cw = [int]($w * 0.8); $ch = [int]($ht * 0.65)
  $bmp = New-Object System.Drawing.Bitmap $cw, $ch
  $g = [System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($x0, $y0, 0, 0, $bmp.Size)
  $white = 0; $n = 0
  for ($y = 0; $y -lt $ch; $y += 12) { for ($x = 0; $x -lt $cw; $x += 12) { $c = $bmp.GetPixel($x, $y); $n++; if ($c.R -gt 245 -and $c.G -gt 245 -and $c.B -gt 245) { $white++ } } }
  $g.Dispose(); $bmp.Dispose()
  if ($n -eq 0) { return -1 }
  return [math]::Round($white / [double]$n, 3)
}
function Ours { Get-CimInstance Win32_Process -Filter "Name='msedge.exe'" | Where-Object { $_.CommandLine -like '*gpucov\edge-prof2*' } }
Remove-Item $o, "$d\edge2-chrome.log", "$d\edge2-gpu.html" -ErrorAction SilentlyContinue
if (Test-Path $prof) { Remove-Item $prof -Recurse -Force -ErrorAction SilentlyContinue }
$edge = @("${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe", "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $edge) { 'EDGE2 result=NOT_INSTALLED' | Out-File $o -Encoding ascii; 'done' | Out-File "$d\edge2-done.txt"; exit }
"EDGE2 version=$((Get-Item $edge).VersionInfo.ProductVersion) url=$Url" | Out-File $o -Encoding ascii
$t0 = Get-Date
$null = Start-Process $edge -ArgumentList "--no-first-run --no-default-browser-check --user-data-dir=$prof --enable-logging --v=1 --log-file=$d\edge2-chrome.log $Extra --new-window $Url" -PassThru
$first = -1; $lastFrac = -1; $samples = @(); $seenGpu = @{}
while (((Get-Date) - $t0).TotalSeconds -lt $Watch) {
  Start-Sleep -Milliseconds 120
  # Each GPU process lives about half a second before its CHECK: list its
  # graphics modules the first time it is seen.
  foreach ($gp in @(Ours | Where-Object { $_.CommandLine -match '--type=gpu-process' })) {
    if ($seenGpu.ContainsKey($gp.ProcessId) -or $seenGpu.Count -ge 4) { continue }
    $seenGpu[$gp.ProcessId] = 1
    $gms = [int]((Get-Date) - $t0).TotalMilliseconds
    "EDGE4 gpu_cmdline pid=$($gp.ProcessId) t=$gms $($gp.CommandLine)" | Out-File $o -Append -Encoding ascii
    Start-Sleep -Milliseconds 150
    $mods = @()
    try { $mods = @((Get-Process -Id $gp.ProcessId -ErrorAction Stop).Modules) } catch { "EDGE4 gpu_mod pid=$($gp.ProcessId) error=$($_.Exception.Message)" | Out-File $o -Append -Encoding ascii }
    "EDGE4 gpu_mod pid=$($gp.ProcessId) count=$($mods.Count)" | Out-File $o -Append -Encoding ascii
    $mods | Where-Object { $_.ModuleName -match 'AppleAgx|d3d|dxgi|libGLES|libEGL|vulkan|swiftshader|warp|dcomp|opengl|nvwgf|igd|mf' } | ForEach-Object { "EDGE4 gpu_mod pid=$($gp.ProcessId) $($_.ModuleName) $($_.FileName)" } | Out-File $o -Append -Encoding ascii
  }
  $ms = [int]((Get-Date) - $t0).TotalMilliseconds
  $hw = [IntPtr]::Zero
  foreach ($p in Ours) { $q = Get-Process -Id $p.ProcessId -ErrorAction SilentlyContinue; if ($q -and $q.MainWindowHandle -ne [IntPtr]::Zero) { $hw = $q.MainWindowHandle; break } }
  if ($hw -eq [IntPtr]::Zero) { continue }
  $f = WhiteFraction $hw
  if ($f -ne $lastFrac -and $samples.Count -lt 40) { $samples += "$($ms):$f" }
  $lastFrac = $f
  if ($first -lt 0 -and $f -ge 0 -and $f -lt 0.9) { $first = $ms }
}
"EDGE2 first_non_white_ms=$first watch_s=$Watch last_white_fraction=$lastFrac" | Out-File $o -Append -Encoding ascii
"EDGE2 samples $($samples -join ' ')" | Out-File $o -Append -Encoding ascii
$gpuProcs = @(Ours | Where-Object { $_.CommandLine -match '--type=gpu-process' })
"EDGE2 gpu_processes_alive=$($gpuProcs.Count) all_processes=$(@(Ours).Count)" | Out-File $o -Append -Encoding ascii
$gpuProcs | Select-Object -First 2 | ForEach-Object { 'EDGE2 gpu_cmd ' + (($_.CommandLine -replace '^.*?--type=gpu-process', '--type=gpu-process').Substring(0, [math]::Min(600, ($_.CommandLine -replace '^.*?--type=gpu-process', '--type=gpu-process').Length))) } | Out-File $o -Append -Encoding ascii
Ours | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
Start-Sleep 2
$h = Start-Process $edge -ArgumentList "--headless=new --user-data-dir=$prof-h --virtual-time-budget=15000 --dump-dom edge://gpu" -RedirectStandardOutput "$d\edge2-gpu.html" -PassThru
if (-not $h.WaitForExit(60000)) { 'EDGE2 gpu_dump=TIMEOUT' | Out-File $o -Append -Encoding ascii }
Get-CimInstance Win32_Process -Filter "Name='msedge.exe'" | Where-Object { $_.CommandLine -like '*gpucov\edge-prof2-h*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
$reports = @(Get-ChildItem "$prof\Crashpad\reports" -File -ErrorAction SilentlyContinue)
"EDGE2 crash_reports=$($reports.Count) " + (($reports | Select-Object -First 5 | ForEach-Object { $_.Name + '@' + $_.LastWriteTime.ToString('HH:mm:ss') }) -join ' ') | Out-File $o -Append -Encoding ascii
'done' | Out-File "$d\edge2-done.txt" -Encoding ascii
