@echo off
echo S261 B11 ENABLE the BthMini/BTHPORT operational event channels (log setting change, diagnostics only), restart BT, dump
wevtutil sl Microsoft-Windows-Bluetooth-Bthmini/Operational /e:true
wevtutil sl Microsoft-Windows-Bluetooth-BthLEPrepairing/Operational /e:true >nul 2>&1
pnputil /restart-device "ACPI\NWOA5F69\0"
ping -n 31 127.0.0.1 >nul
wevtutil qe Microsoft-Windows-Bluetooth-Bthmini/Operational /c:30 /rd:true /f:text
%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe -NoProfile -NonInteractive -Command "Get-WinEvent -ListLog *Bluetooth* -ErrorAction SilentlyContinue | ForEach-Object { 'LOG '+$_.LogName+' enabled='+$_.IsEnabled+' records='+$_.RecordCount }"
echo S261 B11 DONE
exit /b 0
