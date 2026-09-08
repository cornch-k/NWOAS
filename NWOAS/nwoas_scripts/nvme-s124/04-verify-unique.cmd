@echo off
set "TOOLS="
set "TARGET="
for %%d in (C D E F G H I J) do if exist %%d:\NWAGENT.EXE set "TOOLS=%%d:"
for %%d in (C D E F G H I J) do if exist %%d:\S117SRC set "TARGET=%%d:"
if not defined TOOLS exit /b 21
if not defined TARGET exit /b 22
%TOOLS%\NWGUARD.EXE %TARGET% identify
if errorlevel 1 exit /b 23
%TOOLS%\NWREAD.EXE %TARGET%\NWOAS124\B16\R124.BIN 140963987cad885fff805f56f9e49e7bf963c57543223265e5f734b86fe97604 direct
if errorlevel 1 exit /b 30
%TOOLS%\NWREAD.EXE %TARGET%\NWOAS124\B16\R124.BIN 140963987cad885fff805f56f9e49e7bf963c57543223265e5f734b86fe97604 buffered
if errorlevel 1 exit /b 30
%TOOLS%\NWREAD.EXE %TARGET%\NWOAS124\U16\R124.BIN 140963987cad885fff805f56f9e49e7bf963c57543223265e5f734b86fe97604 direct
if errorlevel 1 exit /b 30
%TOOLS%\NWREAD.EXE %TARGET%\NWOAS124\U16\R124.BIN 140963987cad885fff805f56f9e49e7bf963c57543223265e5f734b86fe97604 buffered
if errorlevel 1 exit /b 30
echo S124 UNIQUE-PAGE 16MiB BOTH COPIES POST-REBOOT PASS
exit /b 0
