@echo off
D:\MEM180.EXE 10240 3
if errorlevel 1 exit /b %errorlevel%
D:\CPUSTRES.EXE
if errorlevel 1 exit /b %errorlevel%
D:\DISKREAD.EXE
exit /b %errorlevel%
