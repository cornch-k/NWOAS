# Get-NwWinStat: what the user actually sees in a window. Captures the window's
# screen rectangle (after DWM composition) and reports how many distinct colours
# it holds and how much of it is one colour. A window that only shows a uniform
# grey/black fill has distinct~1 and dominant_frac~1.0.
Add-Type -AssemblyName System.Drawing
if (-not ('NwWin' -as [type])) {
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class NwWin {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
}
'@
}
[void][NwWin]::SetProcessDPIAware()
function Get-NwWinStat([IntPtr]$Handle, [string]$Tag, [string]$Png) {
  $r = New-Object NwWin+RECT
  if ($Handle -eq [IntPtr]::Zero -or -not [NwWin]::GetWindowRect($Handle, [ref]$r)) { return "WINSTAT $Tag result=NO_WINDOW" }
  [void][NwWin]::SetForegroundWindow($Handle); Start-Sleep -Milliseconds 400
  $w = $r.R - $r.L; $h = $r.B - $r.T
  if ($w -le 16 -or $h -le 16) { return "WINSTAT $Tag result=TINY rect=$($r.L),$($r.T),$w,$h iconic=$([NwWin]::IsIconic($Handle))" }
  # skip the title bar / borders: inner 90 %
  $x0 = $r.L + [int]($w * 0.05); $y0 = $r.T + [int]($h * 0.10); $cw = [int]($w * 0.90); $ch = [int]($h * 0.85)
  $bmp = New-Object System.Drawing.Bitmap $cw, $ch
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($x0, $y0, 0, 0, $bmp.Size)
  $hist = @{}; $n = 0
  for ($y = 0; $y -lt $ch; $y += 6) { for ($x = 0; $x -lt $cw; $x += 6) {
    $c = $bmp.GetPixel($x, $y); $k = (($c.R -shr 3) -shl 10) -bor (($c.G -shr 3) -shl 5) -bor ($c.B -shr 3)
    $hist[$k] = 1 + [int]$hist[$k]; $n++ } }
  $top = $hist.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 1
  $tk = [int]$top.Key; $rgb = '{0:x2}{1:x2}{2:x2}' -f ((($tk -shr 10) -band 31) * 8), ((($tk -shr 5) -band 31) * 8), (($tk -band 31) * 8)
  if ($Png) { try { $bmp.Save($Png, [System.Drawing.Imaging.ImageFormat]::Png) } catch {} }
  $g.Dispose(); $bmp.Dispose()
  $frac = [math]::Round($top.Value / [double]$n, 3)
  $verdict = if ($hist.Count -le 4 -or $frac -ge 0.97) { 'BLANK' } else { 'CONTENT' }
  "WINSTAT $Tag result=$verdict rect=$($r.L),$($r.T),$w,$h samples=$n distinct=$($hist.Count) dominant=$rgb dominant_frac=$frac"
}
