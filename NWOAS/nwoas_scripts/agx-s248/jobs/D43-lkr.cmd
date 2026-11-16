@echo off
echo S248 D43 LKR
powershell -NoProfile -Command "Get-ChildItem C:\Windows\LiveKernelReports -Recurse -File -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 6 | ForEach-Object { $f=$_; 'FILE '+$f.FullName+' '+$f.Length+' '+$f.LastWriteTime.ToString('MM-dd HH:mm:ss'); try { $s=[IO.File]::OpenRead($f.FullName); $b=New-Object byte[] 64; [void]$s.Read($b,0,64); $s.Close(); 'HDR '+(($b|ForEach-Object{$_.ToString('x2')}) -join '') } catch { 'ERR '+$_.Exception.Message } }"
exit /b 0
