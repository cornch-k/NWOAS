@echo off
echo GPUCOV K49 EDGE GPU INFO (read-only, no screen capture): Edge in the user session with a private profile and a 127.0.0.1 DevTools port, SystemInfo.getInfo = the edge://gpu data; run 1 default, run 2 with --enable-gpu-rasterization (blocklist override for that process only)
set D=C:\NWOAS\gpucov
if not exist %D%\edgegpu.ps1 (echo K49 edgegpu.ps1 missing - deliver gpucov17 first & echo GPUCOV K49 DONE & exit /b 0)
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
powershell -NoProfile -Command "$t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'K49 explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds"
> %D%\k-egpu1.cmd echo @echo off
>> %D%\k-egpu1.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edgegpu.ps1 -Out %D%\edge-gpuinfo-1.json -Done %D%\egpu1-done.txt -Prof %D%\edge-prof4
> %D%\k-egpu2.cmd echo @echo off
>> %D%\k-egpu2.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edgegpu.ps1 -Out %D%\edge-gpuinfo-2.json -Done %D%\egpu2-done.txt -Prof %D%\edge-prof5 -Port 9334 -Extra "--enable-gpu-rasterization"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K49a -Cmd %D%\k-egpu1.cmd -Done %D%\egpu1-done.txt -Timeout 150
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K49b -Cmd %D%\k-egpu2.cmd -Done %D%\egpu2-done.txt -Timeout 150
powershell -NoProfile -Command "foreach($i in 1,2){ $f='C:\NWOAS\gpucov\edge-gpuinfo-'+$i+'.json'; Get-Content ($f+'.log') -ErrorAction SilentlyContinue | ForEach-Object { 'K49 '+$i+' LOG '+$_ }; if(-not (Test-Path $f)){ 'K49 '+$i+' no json'; continue }; $g=(Get-Content -Raw $f | ConvertFrom-Json).result.gpu; 'K49 '+$i+' FEAT '+(($g.featureStatus.PSObject.Properties | ForEach-Object { $_.Name+'='+$_.Value }) -join ' '); foreach($d in $g.devices){ 'K49 '+$i+(' DEV vendor=0x{0:x} device=0x{1:x} drv={2} {3} | {4}' -f [int64]$d.vendorId,[int64]$d.deviceId,$d.driverVendor,$d.driverVersion,$d.deviceString) }; 'K49 '+$i+' WA '+($g.driverBugWorkarounds -join ','); $a=$g.auxAttributes; foreach($n in 'displayType','glRenderer','glImplementationParts','skiaBackendType','pixelShaderVersion','dx12FeatureLevel','supportsDx12','directComposition','supportsOverlays','maxMsaaSamples','targetCpuBits','processCrashCount','sandboxed'){ 'K49 '+$i+' AUX '+$n+'='+$a.$n } }"
echo GPUCOV K49 DONE
exit /b 0
