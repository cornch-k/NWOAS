@echo off
echo GPUCOV K43b POOL RECEIPT + BUDGET only (S265, probe11; after a restart: the pool came back after the stop path)
set D=C:\NWOAS\gpucov
powershell -NoProfile -Command "Get-CimInstance Win32_VideoController | ForEach-Object { 'K43b VC '+$_.Name+' | '+$_.DriverVersion }"
%D%\nwgpuprobe11-arm64.exe pool dxgi > %D%\probe11-pool.txt 2>&1
powershell -NoProfile -Command "$l=@(Get-Content C:\NWOAS\gpucov\probe11-pool.txt -ErrorAction SilentlyContinue); $l | Where-Object { $_ -match 'NWGP (pool|dxgi adapter=0)' } | ForEach-Object { 'K43b '+$_ }; 'K43b VERDICT pool_demand='+@($l -match 'pool probe present=1 .*window_status=10 ').Count; $o=Get-CimInstance Win32_OperatingSystem; 'K43b OS total_mib='+[int]($o.TotalVisibleMemorySize/1024)+' free_mib='+[int]($o.FreePhysicalMemory/1024)"
echo GPUCOV K43b DONE
exit /b 0
