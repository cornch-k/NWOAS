@echo off
rem S264C T0 (read only): which disks Windows sees, to confirm the T1 rule "2 or more NVMe disks = NS2 present =
rem MacBook-connected boot -> skip D28" before T1 is installed.  Run in a normal MacBook-connected (S268) boot.
echo S264C T0 DISK CHECK
powershell -NoProfile -NonInteractive -Command "$d=@(Get-PhysicalDisk -ErrorAction SilentlyContinue); 'T0 PHYSICAL '+$d.Count+' NVMe '+@($d | Where-Object { $_.BusType -eq 'NVMe' }).Count; foreach($x in $d){ 'T0 DISK id='+$x.DeviceId+' bus='+$x.BusType+' size='+$x.Size+' media='+$x.MediaType+' name='+$x.FriendlyName }; foreach($x in @(Get-Disk -ErrorAction SilentlyContinue)){ 'T0 GETDISK n='+$x.Number+' bus='+$x.BusType+' size='+$x.Size+' parts='+$x.NumberOfPartitions+' offline='+$x.IsOffline }"
echo S264C T0 DONE
exit /b 0
