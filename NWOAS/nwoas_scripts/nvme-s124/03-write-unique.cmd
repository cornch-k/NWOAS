@echo off
set "TOOLS="
set "TARGET="
for %%d in (C D E F G H I J) do if exist %%d:\NWAGENT.EXE set "TOOLS=%%d:"
for %%d in (C D E F G H I J) do if exist %%d:\S117SRC set "TARGET=%%d:"
if not defined TOOLS exit /b 21
if not defined TARGET exit /b 22
%TOOLS%\NWGUARD.EXE %TARGET% identify
if errorlevel 1 exit /b 23
if not exist %TARGET%\NWOAS124 exit /b 24
if exist %TARGET%\NWOAS124\B16 exit /b 25
if exist %TARGET%\NWOAS124\U16 exit /b 26
md %TARGET%\NWOAS124\B16
if errorlevel 1 exit /b 27
md %TARGET%\NWOAS124\U16
if errorlevel 1 exit /b 28
copy /b /y %TOOLS%\R124.BIN %TARGET%\NWOAS124\B16\R124.BIN
if errorlevel 1 exit /b 29
xcopy /j /y %TOOLS%\R124.BIN %TARGET%\NWOAS124\U16\
if errorlevel 1 exit /b 29
%TOOLS%\NWREAD.EXE %TARGET%\NWOAS124\B16\R124.BIN 140963987cad885fff805f56f9e49e7bf963c57543223265e5f734b86fe97604 direct
if errorlevel 1 exit /b 30
%TOOLS%\NWREAD.EXE %TARGET%\NWOAS124\B16\R124.BIN 140963987cad885fff805f56f9e49e7bf963c57543223265e5f734b86fe97604 buffered
if errorlevel 1 exit /b 30
%TOOLS%\NWREAD.EXE %TARGET%\NWOAS124\U16\R124.BIN 140963987cad885fff805f56f9e49e7bf963c57543223265e5f734b86fe97604 direct
if errorlevel 1 exit /b 30
%TOOLS%\NWREAD.EXE %TARGET%\NWOAS124\U16\R124.BIN 140963987cad885fff805f56f9e49e7bf963c57543223265e5f734b86fe97604 buffered
if errorlevel 1 exit /b 30
%TOOLS%\NWVFLUSH.EXE %TARGET% flush
if errorlevel 1 exit /b 31
echo S124 UNIQUE-PAGE 16MiB BOTH COPIES PRE-REBOOT PASS
exit /b 0
