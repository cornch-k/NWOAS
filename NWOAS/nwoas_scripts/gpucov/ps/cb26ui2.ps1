# User session: Cinebench 2026 UI (no test) with the UMD trace; -Warp runs its
# D3D11 devices on WARP through the d3d11.dll proxy the K18 job puts next to
# Cinebench.exe (NWOAS_FORCE_WARP=1, this process only). At 25, 50 and 75 s it
# records the window tree and what each visible window shows, then closes it.
param([int]$Wait = 75, [switch]$Warp, [string]$App = 'C:\NWOAS\cb26\app')
$d = 'C:\NWOAS\gpucov'; $app = $App; $o = "$d\cb26-stat.txt"; $w = "$d\cb26-wintree.txt"
. "$d\winstat.ps1"
. "$d\wintree.ps1"
Remove-Item "$d\cb26-umd.log", "$d\cb26-ui.txt", "$d\cb26-ui-err.txt", $o, $w -ErrorAction SilentlyContinue
$env:APPLE_AGX_UMD_TRACE_FILE = "$d\cb26-umd.log"; $env:APPLE_AGX_UMD_REFUSALS_ONLY = '1'; $env:APPLE_AGX_UMD_TRACE_SYNC = '1'
if ($Warp) { $env:NWOAS_FORCE_WARP = '1' }
if (-not (Test-Path "$app\Cinebench.exe")) { 'CB26UI result=NO_APP' | Out-File $o -Encoding ascii; 'done' | Out-File "$d\cb26-done.txt"; exit }
"CB26UI mode=$(if ($Warp) { 'warp' } else { 'driver' }) d3d11_proxy=$(Test-Path "$app\d3d11.dll") dxgi_proxy=$(Test-Path "$app\dxgi.dll")" | Out-File $o -Encoding ascii
$p = Start-Process -FilePath "$app\Cinebench.exe" -WorkingDirectory $app -RedirectStandardOutput "$d\cb26-ui.txt" -RedirectStandardError "$d\cb26-ui-err.txt" -PassThru
$t0 = Get-Date
foreach ($t in 25, 50, $Wait) {
  while (((Get-Date) - $t0).TotalSeconds -lt $t) { Start-Sleep 1 }
  $q = Get-Process -Id $p.Id -ErrorAction SilentlyContinue
  if (-not $q) { "CB26UI t=$t result=EXITED code=$($p.ExitCode)" | Out-File $o -Append -Encoding ascii; break }
  $q.Refresh()
  ("CB26UI t=$t title=`"" + $q.MainWindowTitle + "`" ws_mib=" + [int]($q.WorkingSet64 / 1MB)) | Out-File $o -Append -Encoding ascii
  (Get-NwWinStat $q.MainWindowHandle "cb26_t$t" "$d\cb26-t$t$(if ($Warp) { '-warp' }).png") | Out-File $o -Append -Encoding ascii
  Get-NwWinTree $p.Id "t$t" | Out-File $w -Append -Encoding ascii
  Get-NwChildStats $p.Id "t$t" | Out-File $w -Append -Encoding ascii
}
Get-Process -Name Cinebench* -ErrorAction SilentlyContinue | Stop-Process -Force
'done' | Out-File "$d\cb26-done.txt" -Encoding ascii
