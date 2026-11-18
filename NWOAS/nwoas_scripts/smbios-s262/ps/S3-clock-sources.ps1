# S262 S3: every clock value Windows offers to programs, and a cycle-counter rate test. Read-only, ~1 s CPU.
Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices;
public static class NwP {
  [StructLayout(LayoutKind.Sequential)] public struct PPI { public uint Number, MaxMhz, CurrentMhz, MhzLimit, MaxIdleState, CurrentIdleState; }
  [DllImport("powrprof.dll")] public static extern uint CallNtPowerInformation(int l, IntPtr i, uint il, [Out] PPI[] o, uint ol);
  [DllImport("kernel32.dll")] public static extern bool QueryThreadCycleTime(IntPtr h, out ulong c);
  [DllImport("kernel32.dll")] public static extern IntPtr GetCurrentThread();
  [DllImport("kernel32.dll")] public static extern UIntPtr SetThreadAffinityMask(IntPtr h, UIntPtr m);
}
'@
$n=[Environment]::ProcessorCount
$a=[Array]::CreateInstance([NwP+PPI],$n)
$r=[NwP]::CallNtPowerInformation(11,[IntPtr]::Zero,0,$a,[uint32](24*$n))
'PPI status=0x'+('{0:x}' -f $r)
foreach($p in $a){ 'PPI cpu='+$p.Number+' MaxMhz='+$p.MaxMhz+' CurrentMhz='+$p.CurrentMhz+' MhzLimit='+$p.MhzLimit }
Get-CimInstance Win32_PerfFormattedData_Counters_ProcessorInformation | ForEach-Object { 'PERF '+$_.Name+' freq='+$_.ProcessorFrequency+' pctMaxFreq='+$_.PercentofMaximumFrequency+' pctPerf='+$_.PercentProcessorPerformance }
'QPF='+[Diagnostics.Stopwatch]::Frequency
$h=[NwP]::GetCurrentThread()
foreach($cpu in 0,4){
  [void][NwP]::SetThreadAffinityMask($h,(New-Object UIntPtr ([uint64](1 -shl $cpu)))); Start-Sleep -Milliseconds 20
  $c0=[uint64]0; $c1=[uint64]0; $sw=[Diagnostics.Stopwatch]::StartNew()
  [void][NwP]::QueryThreadCycleTime($h,[ref]$c0); $x=0; while($sw.ElapsedMilliseconds -lt 500){ $x++ }
  [void][NwP]::QueryThreadCycleTime($h,[ref]$c1); $us=$sw.Elapsed.TotalMilliseconds*1000
  'CYC cpu='+$cpu+' cycles='+($c1-$c0)+' us='+[math]::Round($us)+' cyclesPerUs='+[math]::Round(($c1-$c0)/$us,3)+' loops='+$x
}
