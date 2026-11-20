@echo off
echo GPUCOV K70 POOL DAILY STATUS (read-only): GPU device state, start receipts, pool switches, how much video memory DXGI reports, free RAM
powershell -NoProfile -Command "$d=Get-PnpDevice -InstanceId 'ACPI\APPL0002\0'; 'K70 DEV '+$d.Status+' '+$d.Problem; $k=Get-Item 'HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters'; foreach($n in 'Wom1StartStage','Wom1StartStatus','G3Armed','NwSoftPoolCached','NwSoftOverlay','Wom1PoolCpuVisibleTopDown','NwScanoutPath'){ 'K70 REG '+$n+' = ['+$k.GetValue($n)+']' }; Get-CimInstance Win32_VideoController | ForEach-Object { 'K70 VC '+$_.Name+' | '+$_.DriverVersion+' | '+$_.Status+' | AdapterRAM='+$_.AdapterRAM }; $o=Get-CimInstance Win32_OperatingSystem; 'K70 RAM total_mb='+[int]($o.TotalVisibleMemorySize/1024)+' free_mb='+[int]($o.FreePhysicalMemory/1024)+' boot='+$o.LastBootUpTime.ToString('HH:mm:ss')"
if exist C:\NWOAS\gpucov\nwgpuprobe14-arm64.exe C:\NWOAS\gpucov\nwgpuprobe14-arm64.exe dxgi 2>&1 | findstr /i "adapter budget usage dedicated shared d3d12 feature"
echo GPUCOV K70 DONE
exit /b 0
