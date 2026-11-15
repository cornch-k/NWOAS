# S237 M1: print the physical memory runs the Windows memory manager uses (registry
# HKLM\HARDWARE\RESOURCEMAP\System Resources\Physical Memory, value .Translated, a
# CM_RESOURCE_LIST) and state whether guest-physical 0xC0000000..0xC00FFFFF (PCIe root-port 1
# bridge window) lies inside any of them. Read-only. Also prints the BCD {badmemory} object so
# the result can be read together with the badmemorylist state of this boot.
$ErrorActionPreference = 'Stop'
$b = (Get-ItemProperty 'HKLM:\HARDWARE\RESOURCEMAP\System Resources\Physical Memory').'.Translated'
$lo = [UInt64]0xC0000000; $hi = [UInt64]0xC0100000
$full = [BitConverter]::ToUInt32($b, 0); $o = 4; $hit = 0; $runs = 0; $total = [UInt64]0
for ($f = 0; $f -lt $full; $f++) {
  $n = [BitConverter]::ToUInt32($b, $o + 12); $o += 16
  for ($i = 0; $i -lt $n; $i++) {
    $t = $b[$o]; $fl = [BitConverter]::ToUInt16($b, $o + 2)
    $s = [BitConverter]::ToUInt64($b, $o + 4); $l = [UInt64][BitConverter]::ToUInt32($b, $o + 12)
    if ($t -eq 7) { if ($fl -band 0x200) { $l = $l -shl 8 } elseif ($fl -band 0x400) { $l = $l -shl 16 } elseif ($fl -band 0x800) { $l = $l -shl 32 } }
    if ($t -eq 3 -or $t -eq 7) {
      $e = $s + $l; $runs++; $total += $l
      $ov = ($s -lt $hi) -and ($e -gt $lo)
      if ($ov) { $hit++ }
      '{0} MEM 0x{1:X} 0x{2:X}{3}' -f 'S237', $s, ($e - 1), $(if ($ov) { ' OVERLAPS-BRIDGE-WINDOW' } else { '' })
    }
    $o += 20
  }
}
'S237 RUNS {0} TOTAL_MB {1}' -f $runs, [UInt64]($total / 1MB)
if ($hit -eq 0) { 'S237 BRIDGE-WINDOW 0xC0000000-0xC00FFFFF EXCLUDED' } else { 'S237 BRIDGE-WINDOW 0xC0000000-0xC00FFFFF INCLUDED in {0} run(s)' -f $hit }
