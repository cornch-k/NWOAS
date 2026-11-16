@echo off
echo S248 D34 STATE2
powershell -NoProfile -Command "Get-Date -Format 'HH:mm:ss'"
powershell -NoProfile -Command "$e=@(Get-WinEvent -LogName Application -MaxEvents 200 -ErrorAction SilentlyContinue | Where-Object { $_.Id -eq 1000 -and $_.TimeCreated -gt (Get-Date).AddMinutes(-25) }); 'APPCRASH '+$e.Count; $e | Select-Object -First 3 | ForEach-Object { $_.TimeCreated.ToString('HH:mm:ss')+' '+$_.Properties[0].Value }"
powershell -NoProfile -Command "if(Test-Path C:\NWOAS\umd-trace.log){ $i=Get-Item C:\NWOAS\umd-trace.log; 'TRACE '+$i.Length+' '+$i.LastWriteTime.ToString('HH:mm:ss'); Get-Content C:\NWOAS\umd-trace.log -Tail 4 } else { 'NO TRACE' }"
powershell -NoProfile -Command "(Get-Process dwm).Threads.Count; (Get-Process dwm).CPU"
exit /b 0
