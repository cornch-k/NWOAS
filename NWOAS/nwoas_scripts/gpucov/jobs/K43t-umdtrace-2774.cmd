@echo off
echo GPUCOV K43t 2774 K43c UMD TRACE (read-only): the failure lines of C:\NWOAS\gpucov\k43c-umd.log written by the 08:10 K43c run (memladder2 2304/2560 FAIL), which K43c's own first-40 filter missed; then probe13 rcpt (receipts as stored now)
set D=C:\NWOAS\gpucov
powershell -NoProfile -Command "Get-CimInstance Win32_VideoController | ForEach-Object { 'K43t VC '+$_.Name+' | '+$_.DriverVersion }"
powershell -NoProfile -Command "$i=Get-Item C:\NWOAS\gpucov\k43c-umd.log -ErrorAction SilentlyContinue; if($i){ 'K43t FILE bytes='+$i.Length+' written='+$i.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss') } else { 'K43t FILE missing' }"
powershell -NoProfile -Command "$f='C:\NWOAS\gpucov\k43c-umd.log'; $p='K43t'; $t=@(Get-Content $f -ErrorAction SilentlyContinue); $p+' UMDTRACE file='+$f+' lines='+$t.Count; $t | ForEach-Object { ($_ -split ' ')[0] } | Group-Object | Sort-Object Count -Descending | Select-Object -First 25 | ForEach-Object { $p+' UTSTAGE '+$_.Count+' '+$_.Name }; $t | Where-Object { ($_ -match ' hr=0x[89a-fA-F][0-9a-fA-F]{7} ' -and $_ -notmatch ' hr=0x8000000a ') -or $_ -match '^(reject-|runtime-set-error|device-|measure-cpu-pool-rehome|measure-make-resident|measure-d3d12-heap-refused)' } | Select-Object -First 80 | ForEach-Object { $p+' UTFAIL '+$_.Substring(0,[math]::Min(320,$_.Length)) }; $t | Where-Object { $_ -match '^measure-d3d12-heap hr' } | Select-Object -First 40 | ForEach-Object { $p+' UTHEAP '+$_.Substring(0,[math]::Min(200,$_.Length)) }"
if exist %D%\nwgpuprobe13-arm64.exe (%D%\nwgpuprobe13-arm64.exe rcpt > %D%\k43t-rcpt.txt 2>&1) else (echo K43t probe13 missing)
powershell -NoProfile -Command "$l=@(Get-Content C:\NWOAS\gpucov\k43t-rcpt.txt -ErrorAction SilentlyContinue); 'K43t rcpt lines='+$l.Count; $l | Where-Object { $_ -match 'NWGP (pool probe|pool demand|memladder2|d3d12 device|d3d12 end|begin|end|rcpt)' } | Select-Object -First 160 | ForEach-Object { 'K43t '+$_ }"
echo GPUCOV K43t DONE
exit /b 0
