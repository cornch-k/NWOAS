# K52 helper. Two modes.
#  -Mode run : Edge in the user's session (usersess.ps1) with a private profile and a 127.0.0.1 DevTools port.
#     Opens the static test page, captures the page at 4 scroll positions (Page.captureScreenshot, PNG), runs the
#     page's nwStress() (scroll + zoom changes = every tile rasterized again) for -Secs seconds, captures scroll 0
#     again, records the GPU feature status, the GPU process ids seen and the FATAL / context-lost log lines.
#     Outputs: <Out>-s0..s4.png, <Out>.txt (summary lines "ER <Tag> ..."), <Out>.log, <Out>.chrome.log
#  -Mode cmp : compares run A (default = CPU raster) with run B (--enable-gpu-rasterization) in 8x8 blocks
#     (block mean per channel, so edge anti-aliasing differences average out) and prints the verdict line.
# No input is sent, nothing outside 127.0.0.1 is contacted, the driver is not changed.
param([string]$Mode='run',[string]$Tag='A',[string]$Out='C:\NWOAS\gpucov\er-A',[string]$Done='C:\NWOAS\gpucov\er-A-done.txt',
      [string]$Extra='',[int]$Port=9335,[string]$Prof='C:\NWOAS\gpucov\edge-prof6',
      [string]$Url='file:///C:/NWOAS/gpucov/raster1.html',[int]$Secs=60,
      [string]$A='C:\NWOAS\gpucov\er-A',[string]$B='C:\NWOAS\gpucov\er-B',[string]$Name='K52')
$ErrorActionPreference='Continue'

if($Mode -eq 'cmp'){
  Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System; using System.Drawing; using System.Drawing.Imaging; using System.Runtime.InteropServices;
public static class NwCmp {
  static byte[] Load(string p, out int w, out int h, out int stride){
    using(Bitmap b=new Bitmap(p)){ w=b.Width; h=b.Height;
      BitmapData d=b.LockBits(new Rectangle(0,0,w,h),ImageLockMode.ReadOnly,PixelFormat.Format32bppArgb);
      stride=d.Stride; byte[] a=new byte[stride*h]; Marshal.Copy(d.Scan0,a,0,a.Length); b.UnlockBits(d); return a; } }
  // returns: bad block count (-1 = cannot compare) and a text line
  public static int Cmp(string pa,string pb,out string text){
    int wa,ha,sa,wb,hb,sb; byte[] a,b;
    try { a=Load(pa,out wa,out ha,out sa); b=Load(pb,out wb,out hb,out sb); } catch(Exception e){ text="load_error "+e.Message; return -1; }
    if(wa!=wb||ha!=hb){ text="size_mismatch a="+wa+"x"+ha+" b="+wb+"x"+hb; return -1; }
    const int B=8; int blocks=0,bad=0,x0=99999,y0=99999,x1=-1,y1=-1,worst=0; long sum=0,big=0;
    for(int by=0;by+B<=ha;by+=B) for(int bx=0;bx+B<=wa;bx+=B){
      int[] ma=new int[3]; int[] mb=new int[3];
      for(int y=by;y<by+B;y++){ int oa=y*sa+bx*4, ob=y*sb+bx*4;
        for(int x=0;x<B;x++){ for(int c=0;c<3;c++){ int va=a[oa+c], vb=b[ob+c]; ma[c]+=va; mb[c]+=vb; int df=Math.Abs(va-vb); sum+=df; if(df>64) big++; } oa+=4; ob+=4; } }
      blocks++; int m=0; for(int c=0;c<3;c++) m=Math.Max(m,Math.Abs(ma[c]-mb[c])/(B*B));
      if(m>worst) worst=m;
      if(m>16){ bad++; if(bx<x0)x0=bx; if(by<y0)y0=by; if(bx>x1)x1=bx; if(by>y1)y1=by; } }
    double n=3.0*wa*ha;
    text=String.Format(System.Globalization.CultureInfo.InvariantCulture,"size={0}x{1} blocks={2} bad_blocks={3} bad_pct={4:F3} worst_block_diff={5} mean_diff={6:F3} big_px_pct={7:F3} bad_box={8}",
      wa,ha,blocks,bad,100.0*bad/Math.Max(1,blocks),worst,sum/n,100.0*big/n,bad>0?(x0+","+y0+"-"+(x1+B)+","+(y1+B)):"none");
    return bad; }
  // how much of the picture is not one flat colour (a blank or black capture gives ~0)
  public static string Stat(string p){
    int w,h,s; byte[] a; try { a=Load(p,out w,out h,out s); } catch(Exception e){ return "load_error "+e.Message; }
    int n=0,diff=0; int r0=a[2],g0=a[1],b0=a[0];
    for(int y=0;y<h;y+=4) for(int x=0;x<w;x+=4){ int o=y*s+x*4; n++; if(Math.Abs(a[o+2]-r0)+Math.Abs(a[o+1]-g0)+Math.Abs(a[o]-b0)>24) diff++; }
    return String.Format(System.Globalization.CultureInfo.InvariantCulture,"{0}x{1} not_flat_pct={2:F1}",w,h,100.0*diff/Math.Max(1,n)); }
}
'@
  foreach($p in $A,$B){ Get-Content ($p+'.txt') -ErrorAction SilentlyContinue | ForEach-Object { $Name+' '+$_ }; Get-Content ($p+'.log') -ErrorAction SilentlyContinue | Select-Object -Last 14 | ForEach-Object { $Name+' LOG '+(Split-Path -Leaf $p)+' '+$_ } }
  $fail=@(); $abMax=0.0
  foreach($k in 0,1,2,3){
    $t=''; $n=[NwCmp]::Cmp($A+'-s'+$k+'.png',$B+'-s'+$k+'.png',[ref]$t)
    $Name+' AB shot='+$k+' '+$t+' | A '+[NwCmp]::Stat($A+'-s'+$k+'.png')+' | B '+[NwCmp]::Stat($B+'-s'+$k+'.png')
    if($n -lt 0){ $fail+='ab'+$k+'_nocompare' } elseif($t -match 'bad_pct=([0-9.]+)'){ $v=[double]::Parse($Matches[1],[Globalization.CultureInfo]::InvariantCulture); if($v -gt $abMax){ $abMax=$v }; if($v -gt 1.0){ $fail+='ab'+$k+'_bad_pct' } }
  }
  $self=@{}
  foreach($x in @(@('A',$A),@('B',$B))){
    $t=''; $n=[NwCmp]::Cmp($x[1]+'-s0.png',$x[1]+'-s4.png',[ref]$t); $self[$x[0]]=$n
    $Name+' SELF run='+$x[0]+' before_vs_after_stress '+$t
  }
  if($self['B'] -ne 0){ $fail+='selfB_bad_blocks='+$self['B'] }
  $ta=(Get-Content ($A+'.txt') -ErrorAction SilentlyContinue) -join ' '; $tb=(Get-Content ($B+'.txt') -ErrorAction SilentlyContinue) -join ' '
  $g={ param($s,$k) if($s -match ('(?:^| )'+$k+'=([^ ]+)')){ $Matches[1] } else { '?' } }
  $rb=& $g $tb 'raster'; if($rb -notmatch '^enabled'){ $fail+='raster_B='+$rb }
  foreach($k in 'gpu_pid_count','max_recent_crash','fatals','lost','frames','shots'){
    $v=& $g $tb $k
    if($k -eq 'gpu_pid_count'){ if($v -ne '1'){ $fail+=$k+'='+$v } }
    elseif($k -eq 'frames'){ if($v -eq '?' -or [int]$v -lt 300){ $fail+=$k+'='+$v } }
    elseif($k -eq 'shots'){ if($v -ne '5'){ $fail+=$k+'='+$v } }
    elseif($v -ne '0'){ $fail+=$k+'='+$v }
  }
  $Name+' VERDICT raster_A='+(& $g $ta 'raster')+' raster_B='+$rb+' ab_bad_pct_max='+$abMax.ToString('F3',[Globalization.CultureInfo]::InvariantCulture)+' self_A='+$self['A']+' self_B='+$self['B']+' frames_A='+(& $g $ta 'frames')+' frames_B='+(& $g $tb 'frames')+' gpu_pids_B='+(& $g $tb 'gpu_pid_count')+' fatals_B='+(& $g $tb 'fatals')+' lost_B='+(& $g $tb 'lost')+' => '+$(if($fail.Count){ 'FAIL ('+($fail -join ',')+')' } else { 'PASS' })
  exit 0
}

$log=$Out+'.log'; $sum=$Out+'.txt'; $clog=$Out+'.chrome.log'
function L($m){ Add-Content -LiteralPath $log -Value ((Get-Date).ToString('HH:mm:ss')+' '+$m) }
Remove-Item -LiteralPath $log,$sum,$clog,$Done -ErrorAction SilentlyContinue
foreach($k in 0..4){ Remove-Item -LiteralPath ($Out+'-s'+$k+'.png') -ErrorAction SilentlyContinue }
if(Test-Path -LiteralPath $Prof){ Remove-Item -LiteralPath $Prof -Recurse -Force -ErrorAction SilentlyContinue }
$exe=@((Join-Path ${env:ProgramFiles(x86)} 'Microsoft\Edge\Application\msedge.exe'),(Join-Path $env:ProgramFiles 'Microsoft\Edge\Application\msedge.exe')) | Where-Object { Test-Path $_ } | Select-Object -First 1
$al=@('--remote-debugging-port='+$Port,'--user-data-dir='+$Prof,'--no-first-run','--no-default-browser-check','--window-position=20,20','--window-size=1280,1000','--enable-logging','--log-file='+$clog)
if($Extra){ $al+=$Extra.Split(' ') }
$al+='about:blank'
$shots=0; $frames=-1; $raster='?'; $comp='?'; $gp=@{}; $crash=-1
function GpuPids { Get-CimInstance Win32_Process -Filter "Name='msedge.exe'" -ErrorAction SilentlyContinue | Where-Object { $_.CommandLine -like ('*'+$Prof+'*') -and $_.CommandLine -match '--type=gpu-process' } | ForEach-Object { $script:gp[[string]$_.ProcessId]=1; if($_.CommandLine -match '--gpu-recent-crash-count=([0-9]+)'){ if([int]$Matches[1] -gt $script:crash){ $script:crash=[int]$Matches[1] } } elseif($script:crash -lt 0){ $script:crash=0 } } }
$p=$null
try {
  $p=Start-Process -FilePath $exe -ArgumentList $al -PassThru
  L ('started pid='+$p.Id+' args='+($al -join ' '))
  $bws=$null; $t0=Get-Date
  while(((Get-Date)-$t0).TotalSeconds -lt 60){
    try { $v=Invoke-RestMethod -UseBasicParsing -TimeoutSec 3 -Uri ('http://127.0.0.1:'+$Port+'/json/version'); $bws=$v.webSocketDebuggerUrl; if($bws){ break } } catch {}
    Start-Sleep 2 }
  if(-not $bws){ throw 'no DevTools endpoint' }
  Start-Sleep 6
  $pws=$null
  $tl=@(Invoke-RestMethod -UseBasicParsing -TimeoutSec 5 -Uri ('http://127.0.0.1:'+$Port+'/json/list') | ForEach-Object { $_ } | Where-Object { $_.type -eq 'page' })
  $tp=@($tl | Where-Object { $_.url -eq 'about:blank' }) + $tl | Select-Object -First 1
  if($tp){ $pws=[string]$tp.webSocketDebuggerUrl }
  L ('page_targets='+$tl.Count+' urls='+(($tl | ForEach-Object { ([string]$_.url).Substring(0,[Math]::Min(40,([string]$_.url).Length)) }) -join ' | '))
  L ('browser_ws='+$bws+' page_ws='+$pws)
  if(-not $pws){ throw 'no page target' }
  $ct=[Threading.CancellationToken]::None; $buf=New-Object byte[] 262144; $script:id=0
  function Open($u){ $c=New-Object System.Net.WebSockets.ClientWebSocket; if(-not $c.ConnectAsync([Uri]$u,$ct).Wait(15000)){ throw 'connect timeout' }; $c }
  # Call: sends one DevTools command and returns the raw JSON reply text with the same id (other messages are skipped)
  function Call($c,$method,$params,$waitMs){
    $script:id++; $i=$script:id
    $b=[Text.Encoding]::UTF8.GetBytes('{"id":'+$i+',"method":"'+$method+'","params":'+$params+'}')
    $seg=New-Object 'System.ArraySegment[byte]' -ArgumentList @(,$b)
    if(-not $c.SendAsync($seg,[Net.WebSockets.WebSocketMessageType]::Text,$true,$ct).Wait(15000)){ throw ('send timeout '+$method) }
    for($n=0;$n -lt 200;$n++){
      $ms=New-Object IO.MemoryStream
      do { $rs=New-Object 'System.ArraySegment[byte]' -ArgumentList @(,$buf); $t=$c.ReceiveAsync($rs,$ct); if(-not $t.Wait($waitMs)){ throw ('receive timeout '+$method) }; $r=$t.Result; $ms.Write($buf,0,$r.Count) } while(-not $r.EndOfMessage)
      $j=[Text.Encoding]::UTF8.GetString($ms.ToArray())
      if($j.Substring(0,[Math]::Min(40,$j.Length)) -match ('"id":'+$i+'[,}]')){ return $j } }
    throw ('no reply '+$method) }
  function Shot($c,$k){
    $j=Call $c 'Page.captureScreenshot' '{"format":"png"}' 60000
    if($j -match '"data":"([A-Za-z0-9+/=]+)"'){ [IO.File]::WriteAllBytes($Out+'-s'+$k+'.png',[Convert]::FromBase64String($Matches[1])); $script:shots++; L ('shot '+$k+' base64='+$Matches[1].Length) }
    else { L ('shot '+$k+' failed '+$j.Substring(0,[Math]::Min(200,$j.Length))) } }
  $pg=Open $pws
  [void](Call $pg 'Emulation.setDeviceMetricsOverride' '{"width":1200,"height":900,"deviceScaleFactor":1,"mobile":false}' 30000)
  [void](Call $pg 'Page.navigate' ('{"url":"'+$Url+'"}') 30000)
  Start-Sleep 6
  $j=Call $pg 'Runtime.evaluate' '{"expression":"document.readyState+'' h=''+document.documentElement.scrollHeight+'' stress=''+(typeof nwStress)","returnByValue":true}' 30000
  L ('page '+$(if($j -match '"value":"([^"]*)"'){ $Matches[1] } else { $j.Substring(0,[Math]::Min(200,$j.Length)) }))
  GpuPids
  foreach($k in 0,1,2,3){
    [void](Call $pg 'Runtime.evaluate' ('{"expression":"scrollTo(0,'+($k*900)+');scrollY","returnByValue":true}') 30000)
    Start-Sleep -Milliseconds 1500
    Shot $pg $k }
  [void](Call $pg 'Runtime.evaluate' '{"expression":"scrollTo(0,0);1","returnByValue":true}' 30000)
  $j=Call $pg 'Runtime.evaluate' ('{"expression":"nwStress('+($Secs*1000)+')","awaitPromise":true,"returnByValue":true}') (($Secs+30)*1000)
  if($j -match '"value":([0-9]+)'){ $frames=[int]$Matches[1] }
  L ('stress frames='+$frames+' secs='+$Secs)
  GpuPids
  Start-Sleep 2
  Shot $pg 4
  $br=Open $bws
  $j=Call $br 'SystemInfo.getInfo' '{}' 60000
  if($j -match '"rasterization":"([a-z_]+)"'){ $raster=$Matches[1] }
  if($j -match '"gpu_compositing":"([a-z_]+)"'){ $comp=$Matches[1] }
  L ('info bytes='+$j.Length+' rasterization='+$raster+' gpu_compositing='+$comp)
  GpuPids
  try { [void](Call $br 'Browser.close' '{}' 5000) } catch {}
} catch { L ('error '+$_.Exception.Message) }
Start-Sleep 3
Get-CimInstance Win32_Process -Filter "Name='msedge.exe'" -ErrorAction SilentlyContinue | Where-Object { $_.CommandLine -like ('*'+$Prof+'*') } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
$cl=@(Get-Content -LiteralPath $clog -ErrorAction SilentlyContinue)
$fat=@($cl -match ':FATAL:').Count
$lostl=@($cl | Where-Object { $_ -match 'Exiting GPU process|context lost|ContextLost|context_lost|Lost context|GL_OUT_OF_MEMORY|0x887A0005|DEVICE_REMOVED|GPU process exited|GPU process crashed' })
$lostl | Select-Object -First 6 | ForEach-Object { L ('chrome '+$_.Substring(0,[Math]::Min(260,$_.Length))) }
Set-Content -LiteralPath $sum -Encoding ascii -Value ('ER '+$Tag+' extra=['+$Extra+'] raster='+$raster+' gpu_compositing='+$comp+' shots='+$shots+' frames='+$frames+' secs='+$Secs+' gpu_pid_count='+$gp.Count+' gpu_pids='+(($gp.Keys | Sort-Object) -join ',')+' max_recent_crash='+$crash+' fatals='+$fat+' lost='+$lostl.Count+' chrome_log_lines='+$cl.Count)
Set-Content -LiteralPath $Done -Value 'done'
