@echo off
if /i not "%SystemDrive%"=="C:" exit /b 20
reg query HKLM\SYSTEM\CurrentControlSet\Control\MiniNT >nul 2>&1
if not errorlevel 1 exit /b 21
echo S129 INSTALLED WINDOWS USERSPACE RESPONDING
ver
echo SystemDrive=%SystemDrive%
echo SystemRoot=%SystemRoot%
echo PROCESSOR_ARCHITECTURE=%PROCESSOR_ARCHITECTURE%
echo NUMBER_OF_PROCESSORS=%NUMBER_OF_PROCESSORS%
reg query HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion /v ProductName
reg query HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion /v CurrentBuildNumber
reg query HKLM\SYSTEM\Setup /v SystemSetupInProgress
reg query HKLM\SYSTEM\Setup /v OOBEInProgress
reg query HKLM\SYSTEM\CurrentControlSet\Services\stornvme /v Start
bcdedit /enum {current}
dir C:\Windows\Panther\*.log
if exist C:\Windows\Panther\setuperr.log type C:\Windows\Panther\setuperr.log
echo S129 INSTALLED USERSPACE REPORT COMPLETE
exit /b 0
