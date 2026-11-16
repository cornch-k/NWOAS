@echo off
echo S248 D36 CRASHES
powershell -NoProfile -Command "Get-WinEvent -LogName Application -MaxEvents 300 -ErrorAction SilentlyContinue | Where-Object { ($_.Id -eq 1000 -or $_.Id -eq 1001 -or $_.Id -eq 1002) -and $_.TimeCreated -gt (Get-Date).AddMinutes(-90) } | Select-Object -First 12 | ForEach-Object { $_.TimeCreated.ToString('HH:mm:ss')+' '+$_.Id+' '+$_.Properties[0].Value+' '+$_.Properties[6].Value+' '+$_.Properties[7].Value }"
powershell -NoProfile -Command "Get-Process explorer,dwm -ErrorAction SilentlyContinue | ForEach-Object { $_.Name+' '+$_.Id+' START '+$_.StartTime.ToString('HH:mm:ss') }; Get-Date -Format HH:mm:ss"
exit /b 0
