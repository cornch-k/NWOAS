# K49 helper (read-only): starts Edge with a private profile and a local DevTools port, asks the browser
# target for SystemInfo.getInfo (the data edge://gpu shows: devices, driver version, featureStatus,
# driver bug workarounds, aux attributes) and saves the JSON reply. No screen capture, no input, no network
# beyond 127.0.0.1. Run in the user's session (usersess.ps1).
param([string]$Out='C:\NWOAS\gpucov\edge-gpuinfo.json',[string]$Done='C:\NWOAS\gpucov\edgegpu-done.txt',
      [string]$Extra='',[int]$Port=9333,[string]$Prof='C:\NWOAS\gpucov\edge-prof4')
$ErrorActionPreference='Continue'
$log=$Out+'.log'
function L($m){ Add-Content -LiteralPath $log -Value ((Get-Date).ToString('HH:mm:ss')+' '+$m) }
Remove-Item -LiteralPath $Out,$log,$Done -ErrorAction SilentlyContinue
$exe=Join-Path ${env:ProgramFiles(x86)} 'Microsoft\Edge\Application\msedge.exe'
$al=@('--remote-debugging-port='+$Port,'--user-data-dir='+$Prof,'--no-first-run','--no-default-browser-check')
if($Extra){ $al+=$Extra.Split(' ') }
$al+='about:blank'
$p=Start-Process -FilePath $exe -ArgumentList $al -PassThru
L ('started pid='+$p.Id+' args='+($al -join ' '))
$ws=$null; $t0=Get-Date
while(((Get-Date)-$t0).TotalSeconds -lt 60){
  try { $v=Invoke-RestMethod -UseBasicParsing -TimeoutSec 3 -Uri ('http://127.0.0.1:'+$Port+'/json/version'); $ws=$v.webSocketDebuggerUrl; if($ws){ break } } catch {}
  Start-Sleep 2 }
L ('ws='+$ws)
Start-Sleep 10
$c=$null
try {
  $c=New-Object System.Net.WebSockets.ClientWebSocket
  $ct=[Threading.CancellationToken]::None
  $c.ConnectAsync([Uri]$ws,$ct).Wait()
  $buf=New-Object byte[] 65536
  $send={ param($s) $b=[Text.Encoding]::UTF8.GetBytes($s); $seg=New-Object 'System.ArraySegment[byte]' -ArgumentList @(,$b); $c.SendAsync($seg,[Net.WebSockets.WebSocketMessageType]::Text,$true,$ct).Wait() }
  $recv={ $ms=New-Object IO.MemoryStream; do { $seg=New-Object 'System.ArraySegment[byte]' -ArgumentList @(,$buf); $r=$c.ReceiveAsync($seg,$ct).Result; $ms.Write($buf,0,$r.Count) } while(-not $r.EndOfMessage); [Text.Encoding]::UTF8.GetString($ms.ToArray()) }
  & $send '{"id":1,"method":"SystemInfo.getInfo"}'
  $j=& $recv; $n=0
  while($j -notmatch '"id":1[,}]' -and $n -lt 50){ $j=& $recv; $n++ }
  Set-Content -LiteralPath $Out -Value $j -Encoding UTF8
  L ('info bytes='+$j.Length)
  & $send '{"id":2,"method":"Browser.close"}'
} catch { L ('error '+$_.Exception.Message) }
Start-Sleep 3
if($p -and -not $p.HasExited){ Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; L 'stopped' }
Set-Content -LiteralPath $Done -Value 'done'
