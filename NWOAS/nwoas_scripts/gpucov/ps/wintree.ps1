# Get-NwWinTree: every top-level window of a process and all their descendants
# (class, title, rect, visible, style, extended style), one "WT" line each.
# Get-NwChildStats: WINSTAT for each visible window of the tree larger than
# 20x20 (what the user sees in that rectangle). Needs winstat.ps1 first.
if (-not ('NwTree' -as [type])) {
Add-Type @'
using System; using System.Collections.Generic; using System.Runtime.InteropServices; using System.Text;
public static class NwTree {
  public delegate bool EnumProc(IntPtr h, IntPtr p);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent, EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr h);
  [DllImport("user32.dll", EntryPoint="GetWindowLongPtrW")] public static extern IntPtr GetLong(IntPtr h, int i);
  [StructLayout(LayoutKind.Sequential)] public struct R { public int L, T, Rt, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out R r);
  public static string Cls(IntPtr h) { var s = new StringBuilder(128); GetClassName(h, s, 128); return s.ToString(); }
  public static string Txt(IntPtr h) { var s = new StringBuilder(64); GetWindowText(h, s, 64); return s.ToString(); }
  public static List<IntPtr> Tops(uint pid) { var l = new List<IntPtr>(); EnumWindows((h, p) => { uint q; GetWindowThreadProcessId(h, out q); if (q == pid) l.Add(h); return true; }, IntPtr.Zero); return l; }
  public static List<IntPtr> Kids(IntPtr top) { var l = new List<IntPtr>(); EnumChildWindows(top, (h, p) => { l.Add(h); return true; }, IntPtr.Zero); return l; }
}
'@
}
function Get-NwWinTree([int]$ProcId, [string]$Tag) {
  $out = @(); $all = @()
  foreach ($t in [NwTree]::Tops([uint32]$ProcId)) { $all += ,@($t, 0); foreach ($k in [NwTree]::Kids($t)) { $all += ,@($k, 1) } }
  foreach ($e in $all) {
    $h = $e[0]; $r = New-Object NwTree+R; [void][NwTree]::GetWindowRect($h, [ref]$r)
    $out += ('WT {0} {1} hwnd={2:x} parent={3:x} class="{4}" title="{5}" rect={6},{7},{8}x{9} visible={10} style={11:x8} exstyle={12:x8}' -f $Tag, ($(if ($e[1]) { 'child' } else { 'top' })), [int64]$h, [int64][NwTree]::GetParent($h), [NwTree]::Cls($h), ([NwTree]::Txt($h) -replace '[^ -~]', '?'), $r.L, $r.T, ($r.Rt - $r.L), ($r.B - $r.T), [int][NwTree]::IsWindowVisible($h), [int64][NwTree]::GetLong($h, -16), [int64][NwTree]::GetLong($h, -20))
  }
  if (-not $out) { $out = @("WT $Tag none pid=$ProcId") }
  $out
}
function Get-NwChildStats([int]$ProcId, [string]$Tag) {
  $n = 0
  foreach ($t in [NwTree]::Tops([uint32]$ProcId)) {
    foreach ($h in @($t) + @([NwTree]::Kids($t))) {
      $r = New-Object NwTree+R; [void][NwTree]::GetWindowRect($h, [ref]$r)
      if (-not [NwTree]::IsWindowVisible($h) -or ($r.Rt - $r.L) -lt 20 -or ($r.B - $r.T) -lt 20) { continue }
      if (++$n -gt 40) { return }
      (Get-NwWinStat $h ('{0}_{1:x}' -f $Tag, [int64]$h) $null)
    }
  }
}
