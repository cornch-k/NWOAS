$ErrorActionPreference='Stop'
$base=$env:PUBLIC+'\Documents\NWOAS-BENCH\cb-s218'
if((Test-Path ($base+'.out')) -or (Test-Path ($base+'.result'))){throw 'S218 output already exists'}
$i=New-Object Diagnostics.ProcessStartInfo
$i.FileName='C:\NWOAS-BENCH\Cinebench2026\Cinebench.exe'
$i.WorkingDirectory='C:\NWOAS-BENCH\Cinebench2026'
$i.Arguments='g_CinebenchCpuXTest=true'
$i.UseShellExecute=$false
$i.CreateNoWindow=$true
$i.RedirectStandardOutput=$true
$i.RedirectStandardError=$true
$p=New-Object Diagnostics.Process
$p.StartInfo=$i
if(-not $p.Start()){throw 'Cinebench start failed'}
$handle=$p.Handle
$outTask=$p.StandardOutput.ReadToEndAsync()
$errTask=$p.StandardError.ReadToEndAsync()
('START_UTC='+[DateTime]::UtcNow.ToString('o')+' PID='+$p.Id+' SESSION='+$p.SessionId) | Set-Content ($base+'.result')
$timer=[Diagnostics.Stopwatch]::StartNew()
$timedOut=$false
while(-not $p.WaitForExit(1000)){
 if($timer.Elapsed.TotalSeconds -gt 1800){$timedOut=$true;$p.Kill();break}
}
$p.WaitForExit()
$nativeCode=$p.ExitCode
[IO.File]::WriteAllText($base+'.out',$outTask.Result)
[IO.File]::WriteAllText($base+'.err',$errTask.Result)
('NATIVE_EXIT='+$nativeCode+' TIMEOUT='+$timedOut+' ELAPSED_S='+$timer.Elapsed.TotalSeconds) | Add-Content ($base+'.result')
('END_UTC='+[DateTime]::UtcNow.ToString('o')) | Add-Content ($base+'.result')
$p.Dispose()
if($timedOut){exit 124}
exit $nativeCode
