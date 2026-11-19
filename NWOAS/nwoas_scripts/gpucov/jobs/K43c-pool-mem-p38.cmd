@echo off
echo GPUCOV K43c POOL MEMORY P38 (2774, probe12): fresh D3D12 device per size 1792..3072 MiB with first/last-chunk probe copies and full content check (UMD refusal trace on), 256 MiB heaps with per-heap tag readback until creation fails, D3D11 64..1024 MiB, KMD receipts after each failure
set D=C:\NWOAS\gpucov
powershell -NoProfile -Command "Get-CimInstance Win32_VideoController | ForEach-Object { 'K43c VC '+$_.Name+' | '+$_.DriverVersion }"
del /q %D%\probe12-ladder.txt %D%\probe12-fill.txt %D%\k43c-umd.log 2>nul
powershell -NoProfile -Command "$o=Get-CimInstance Win32_OperatingSystem; 'K43c OS before total_mib='+[int]($o.TotalVisibleMemorySize/1024)+' free_mib='+[int]($o.FreePhysicalMemory/1024)"
setlocal
set APPLE_AGX_UMD_TRACE_FILE=%D%\k43c-umd.log
set APPLE_AGX_UMD_REFUSALS_ONLY=1
set APPLE_AGX_UMD_TRACE_SYNC=1
%D%\nwgpuprobe12-arm64.exe pool dxgi memladder2 ladder=1792,2048,2304,2560,3072 > %D%\probe12-ladder.txt 2>&1
endlocal
%D%\nwgpuprobe12-arm64.exe fill3 d3d11mem rcpt dxgi > %D%\probe12-fill.txt 2>&1
powershell -NoProfile -Command "$o=Get-CimInstance Win32_OperatingSystem; 'K43c OS after total_mib='+[int]($o.TotalVisibleMemorySize/1024)+' free_mib='+[int]($o.FreePhysicalMemory/1024)"
powershell -NoProfile -Command "$l=@(Get-Content C:\NWOAS\gpucov\probe12-ladder.txt -ErrorAction SilentlyContinue) + @(Get-Content C:\NWOAS\gpucov\probe12-fill.txt -ErrorAction SilentlyContinue); 'K43c lines='+$l.Count; $l | Where-Object { $_ -match 'NWGP (pool|dxgi adapter=0|memladder2|fill3|d3d11mem|d3d12 device|d3d12 end|begin|end|rcpt)' } | Select-Object -First 200 | ForEach-Object { 'K43c '+$_ }; $p=@($l -match 'pool probe present=1 .*window_status=10 ').Count; $s=@($l -match 'memladder2 summary ') | Select-Object -First 1; $lp=0; if($s -match 'largest_pass_mib=(\d+)'){ $lp=[int]$Matches[1] }; $f=@($l -match 'fill3 heaps_256mib=') | Select-Object -First 1; $fh=0; $te='0/0'; $fr='none'; $fs='none'; if($f -match 'heaps_256mib=(\d+)'){ $fh=[int]$Matches[1] }; if($f -match 'tags_end=(\d+/\d+)'){ $te=$Matches[1] }; if($f -match 'removed=(0x[0-9a-f]+)'){ $fr=$Matches[1] }; if($f -match 'stop_hr=(0x[0-9a-f]+)'){ $fs=$Matches[1] }; $tp=$te.Split('/'); $fok=($fh -ge 12) -and ($tp[0] -eq $tp[1]) -and ($fr -eq '0x00000000') -and (($fs -eq '0x8007000e') -or ($fs -eq '0x00000000')); $d=@($l -match 'd3d11mem size_mib=.*(mismatches=[1-9]|removed=0x[1-9a-f]|stage=map)').Count; 'K43c VERDICT pool_demand='+$p+' ladder_largest_pass_mib='+$lp+' fill3_heaps='+$fh+' fill3_tags_end='+$te+' fill3_stop='+$fs+' fill3_removed='+$fr+' d3d11_bad='+$d+' => '+$(if($p -and ($lp -ge 3072) -and $fok -and ($d -eq 0)){'PASS'}else{'FAIL'})"
powershell -NoProfile -Command "$t=@(Get-Content C:\NWOAS\gpucov\k43c-umd.log -ErrorAction SilentlyContinue); 'K43c UMDTRACE lines='+$t.Count; $t | Where-Object { $_ -match 'runtime-set-error|measure-d3d12-rejected|measure-d3d12-heap|g4-native|reject-|measure-gpuva-paging-wait|measure-gpuva-map-time|backend-fail' } | Select-Object -First 40 | ForEach-Object { 'K43c UT '+$_.Substring(0,[math]::Min(300,$_.Length)) }"
powershell -NoProfile -Command "Get-WinEvent -FilterHashtable @{LogName='System'; StartTime=(Get-Date).AddMinutes(-20)} -ErrorAction SilentlyContinue | Where-Object { $_.ProviderName -match 'Display|dxgkrnl|DxgKrnl|Resource-Exhaustion' -or $_.Id -in 4101,2004 } | Select-Object -First 8 | ForEach-Object { 'K43c SYSEVT '+$_.TimeCreated.ToString('HH:mm:ss')+' '+$_.ProviderName+' '+$_.Id }"
echo GPUCOV K43c DONE
exit /b 0
