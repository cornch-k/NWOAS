@echo off
powershell -NoProfile -Command "$ErrorActionPreference='Continue'; Get-CimInstance Win32_PageFileUsage | Select Name,AllocatedBaseSize,CurrentUsage,PeakUsage | Format-List; Get-ChildItem -LiteralPath 'C:\Users\NWOAS\AppData\Local\Temp','C:\Windows\Temp','C:\Windows\Minidump','C:\NWOAS-BENCH','C:\Recovery' -File -Recurse -Force -ErrorAction SilentlyContinue | Sort-Object Length -Descending | Select -First 20 FullName,Length | Format-Table -AutoSize; Get-ChildItem -LiteralPath 'C:\Windows' -Filter '*.dmp' -Force | Select FullName,Length | Format-List"
exit /b 0
