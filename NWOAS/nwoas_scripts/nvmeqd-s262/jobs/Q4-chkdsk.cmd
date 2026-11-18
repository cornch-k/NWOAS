@echo off
echo S262 Q4 CHKDSK SCAN C: (read-only online scan)
chkdsk C: /scan
echo S262 Q4 DONE chkdsk_rc=%ERRORLEVEL%
powershell -NoProfile -Command "Get-WinEvent -FilterHashtable @{LogName='System'; Id=7,51,129,153; StartTime=(Get-Date).AddHours(-3)} -ErrorAction SilentlyContinue | ForEach-Object { 'DISKEVT '+$_.TimeCreated.ToString('HH:mm:ss')+' '+$_.ProviderName+' id='+$_.Id }; 'DISKEVT_COUNT '+@(Get-WinEvent -FilterHashtable @{LogName='System'; Id=7,51,129,153; StartTime=(Get-Date).AddHours(-3)} -ErrorAction SilentlyContinue).Count"
