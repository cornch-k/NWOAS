@echo off
rem S268R R3 = nvmeqd-s262/jobs/Q4-chkdsk.cmd with the disk event window = THIS boot (Q4: last 3 h).
rem Provider-aware as Q4: disk 7,11,51,153,154,157; stornvme/storport 129; Ntfs 50,55,140.  Kernel-Boot 153 is the
rem once-per-boot VBS status information event, not a disk error, and is reported separately.
echo S268R R3 CHKDSK SCAN C: (online scan, as Q4)
chkdsk C: /scan
echo S268R R3 chkdsk_rc=%ERRORLEVEL%
powershell -NoProfile -Command "$s=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime; 'R3 SINCE '+$s.ToString('s'); $a=@(Get-WinEvent -FilterHashtable @{LogName='System'; Id=7,11,50,51,55,129,140,153,154,157; StartTime=$s} -ErrorAction SilentlyContinue); $d=@($a | Where-Object { ($_.ProviderName -eq 'disk' -and @(7,11,51,153,154,157) -contains $_.Id) -or ($_.Id -eq 129 -and $_.ProviderName -match 'stornvme|storport|StorPort') -or ($_.ProviderName -match '^(Microsoft-Windows-)?Ntfs$' -and @(50,55,140) -contains $_.Id) }); $b=@($a | Where-Object { $_.ProviderName -eq 'Microsoft-Windows-Kernel-Boot' -and $_.Id -eq 153 }); $d | Select-Object -First 30 | ForEach-Object { 'DISKEVT '+$_.TimeCreated.ToString('HH:mm:ss')+' '+$_.ProviderName+' id='+$_.Id }; 'DISKEVT_COUNT '+$d.Count; 'BOOT153_COUNT '+$b.Count+' (Kernel-Boot VBS status, not a disk error)'"
echo S268R R3 DONE
exit /b 0
