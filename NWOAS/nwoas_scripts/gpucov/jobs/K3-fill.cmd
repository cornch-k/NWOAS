@echo off
echo GPUCOV K3 FILL: 64 MiB DEFAULT heaps until failure (cap 1 GiB), budgets before/after
set D=C:\NWOAS\gpucov
%D%\nwgpuprobe-arm64.exe dxgi fill dxgi > %D%\probe-fill.txt 2>&1
powershell -NoProfile -Command "Get-Content C:\NWOAS\gpucov\probe-fill.txt | Select-String -Pattern 'group=local|fill|device|end' | Select-Object -First 30 | ForEach-Object { 'K3 '+$_.Line }"
echo GPUCOV K3 DONE
exit /b 0
