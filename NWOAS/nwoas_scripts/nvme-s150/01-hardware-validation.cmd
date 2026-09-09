@echo off
setlocal EnableExtensions
echo S150 HARDWARE VALIDATION BEGIN
echo DATE=%DATE% TIME=%TIME%
echo PROCESSORS=%NUMBER_OF_PROCESSORS% ARCH=%PROCESSOR_ARCHITECTURE%
ver
for %%D in (D E F G H I) do if exist %%D:\NWOS.EXE (
  echo LINK_DRIVE=%%D:
  dir %%D:\
  certutil.exe -hashfile %%D:\NWOS.EXE SHA256
)
echo --- DISKS ---
powershell.exe -NoLogo -NoProfile -Command "Get-Disk | Sort-Object Number | Format-Table Number,FriendlyName,BusType,PartitionStyle,OperationalStatus,HealthStatus,Size -AutoSize"
echo --- PROCESSOR ---
powershell.exe -NoLogo -NoProfile -Command "$p=Get-CimInstance Win32_Processor; $p | Format-List Name,NumberOfCores,NumberOfLogicalProcessors,MaxClockSpeed,CurrentClockSpeed"
echo --- PROBLEM DEVICES ---
powershell.exe -NoLogo -NoProfile -Command "Get-PnpDevice | Where-Object Status -ne 'OK' | Select-Object -First 80 Class,FriendlyName,InstanceId,Status,Problem | Format-Table -Wrap"
echo --- C DRIVE SAMPLE ---
fsutil.exe fsinfo volumeinfo C:
where.exe winsat.exe
if not errorlevel 1 winsat.exe disk -seq -read -drive c
echo S150 HARDWARE VALIDATION COMPLETE
exit /b 0
