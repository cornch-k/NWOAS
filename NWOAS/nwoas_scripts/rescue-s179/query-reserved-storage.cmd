@echo off
dism.exe /Online /Get-ReservedStorageState
exit /b %errorlevel%
