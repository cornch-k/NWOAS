# User session: Edge + WebGL/WebGPU/decode page. (1) headless --dump-dom for the
# capability lines, (2) a normal window for 10 s: frame count via the title and
# what the window shows. Uses its own profile under C:\NWOAS\gpucov; only Edge
# processes started with that profile are closed afterwards.
$d='C:\NWOAS\gpucov'; $o="$d\edge-stat.txt"; $url='file:///C:/NWOAS/gpucov/webgl.html'
. "$d\winstat.ps1"
Remove-Item $o,"$d\edge-headless.html" -ErrorAction SilentlyContinue
$edge = @("${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe", "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $edge) { 'EDGE result=NOT_INSTALLED' | Out-File $o -Encoding ascii; 'done' | Out-File "$d\edge-done.txt"; exit }
"EDGE path=`"$edge`" version=$((Get-Item $edge).VersionInfo.ProductVersion)" | Out-File $o -Encoding ascii
function Stop-Ours { Get-CimInstance Win32_Process -Filter "Name='msedge.exe'" | Where-Object { $_.CommandLine -like '*gpucov\edge-prof*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue } }
$h = Start-Process $edge -ArgumentList "--headless=new --no-first-run --user-data-dir=$d\edge-prof-h --virtual-time-budget=20000 --dump-dom ${url}?run=3" -RedirectStandardOutput "$d\edge-headless.html" -PassThru
if (-not $h.WaitForExit(90000)) { 'EDGE headless result=TIMEOUT' | Out-File $o -Append -Encoding ascii }
Stop-Ours
Select-String -Path "$d\edge-headless.html" -Pattern "NWGL [a-z0-9_]+=" -ErrorAction SilentlyContinue | ForEach-Object { 'EDGE headless ' + ($_.Line -replace '<[^>]+>', '') } | Out-File $o -Append -Encoding ascii
$e = Start-Process $edge -ArgumentList "--no-first-run --no-default-browser-check --user-data-dir=$d\edge-prof --new-window --window-size=900,700 ${url}?run=10" -PassThru
$t0 = Get-Date; $win = $null
while (((Get-Date) - $t0).TotalSeconds -lt 40) {
  Start-Sleep 2
  $win = Get-Process msedge -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowTitle -like 'NWGL*' } | Select-Object -First 1
  if ($win -and $win.MainWindowTitle -like '*done=1*') { break }
}
if ($win) {
  "EDGE window title=`"$($win.MainWindowTitle)`"" | Out-File $o -Append -Encoding ascii
  (Get-NwWinStat $win.MainWindowHandle 'edge' "$d\edge.png") | Out-File $o -Append -Encoding ascii
} else { 'EDGE window result=NO_NWGL_WINDOW' | Out-File $o -Append -Encoding ascii }
Stop-Ours
'done' | Out-File "$d\edge-done.txt" -Encoding ascii
