@echo off
echo S262 Q4 CHKDSK SCAN C: (read-only online scan)
chkdsk C: /scan
echo S262 Q4 DONE chkdsk_rc=%ERRORLEVEL%
rem Disk error events of the last 3 h, by provider AND id (g262R).  Microsoft-Windows-Kernel-Boot 153 is the
rem once-per-boot "Virtualization-based security (policies: ...) is ..." information event, not a disk error;
rem disk 153 ("The IO operation at logical block address ... was retried") is.  Counted: disk 7,11,51,153,154,157;
rem storport/miniport 129 (reset to device); Ntfs 50,55,140.  Kernel-Boot 153 is reported separately.
powershell -NoProfile -Command "$s=(Get-Date).AddHours(-3); $a=@(Get-WinEvent -FilterHashtable @{LogName='System'; Id=7,11,50,51,55,129,140,153,154,157; StartTime=$s} -ErrorAction SilentlyContinue); $d=@($a | Where-Object { ($_.ProviderName -eq 'disk' -and @(7,11,51,153,154,157) -contains $_.Id) -or ($_.Id -eq 129 -and $_.ProviderName -match 'stornvme|storport|StorPort') -or ($_.ProviderName -match '^(Microsoft-Windows-)?Ntfs$' -and @(50,55,140) -contains $_.Id) }); $b=@($a | Where-Object { $_.ProviderName -eq 'Microsoft-Windows-Kernel-Boot' -and $_.Id -eq 153 }); $d | ForEach-Object { 'DISKEVT '+$_.TimeCreated.ToString('HH:mm:ss')+' '+$_.ProviderName+' id='+$_.Id }; 'DISKEVT_COUNT '+$d.Count; 'BOOT153_COUNT '+$b.Count+' (Kernel-Boot VBS status, once per boot, not a disk error)'"
