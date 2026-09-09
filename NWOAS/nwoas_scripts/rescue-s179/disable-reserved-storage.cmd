@echo off
dism.exe /Online /Set-ReservedStorageState /State:Disabled
if errorlevel 1 exit /b %errorlevel%
dism.exe /Online /Get-ReservedStorageState
fsutil volume diskfree C:
exit /b %errorlevel%
