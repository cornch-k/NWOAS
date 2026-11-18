# User session: start the Cinebench 2026 UI (no test), with the AGX UMD trace on
# for this process only, sample what the window shows, then close it.
param([int]$Wait=75)
$d='C:\NWOAS\gpucov'; $app='C:\NWOAS\cb26\app'; $o="$d\cb26-stat.txt"
. "$d\winstat.ps1"
Remove-Item "$d\cb26-umd.log","$d\cb26-ui.txt","$d\cb26-ui-err.txt",$o -ErrorAction SilentlyContinue
$env:APPLE_AGX_UMD_TRACE_FILE="$d\cb26-umd.log"; $env:APPLE_AGX_UMD_REFUSALS_ONLY='1'; $env:APPLE_AGX_UMD_TRACE_SYNC='1'
if (-not (Test-Path "$app\Cinebench.exe")) { 'CB26UI result=NO_APP' | Out-File $o -Encoding ascii; 'done' | Out-File "$d\cb26-done.txt"; exit }
$p = Start-Process -FilePath "$app\Cinebench.exe" -WorkingDirectory $app -RedirectStandardOutput "$d\cb26-ui.txt" -RedirectStandardError "$d\cb26-ui-err.txt" -PassThru
$t0 = Get-Date
foreach ($t in 25, 50, $Wait) {
  while (((Get-Date) - $t0).TotalSeconds -lt $t) { Start-Sleep 1 }
  $q = Get-Process -Id $p.Id -ErrorAction SilentlyContinue
  if (-not $q) { "CB26UI t=$t result=EXITED code=$($p.ExitCode)" | Out-File $o -Append -Encoding ascii; break }
  $q.Refresh()
  ("CB26UI t=$t title=`"" + $q.MainWindowTitle + "`" ws_mib=" + [int]($q.WorkingSet64/1MB)) | Out-File $o -Append -Encoding ascii
  (Get-NwWinStat $q.MainWindowHandle "cb26_t$t" "$d\cb26-t$t.png") | Out-File $o -Append -Encoding ascii
}
Get-Process -Name Cinebench* -ErrorAction SilentlyContinue | Stop-Process -Force
'done' | Out-File "$d\cb26-done.txt" -Encoding ascii
