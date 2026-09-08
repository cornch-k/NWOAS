@echo off
set "TOOLS="
set "TARGET="
for %%d in (C D E F G H I J) do if exist %%d:\NWAGENT.EXE set "TOOLS=%%d:"
for %%d in (C D E F G H I J) do if exist %%d:\S117SRC set "TARGET=%%d:"
if not defined TOOLS exit /b 21
if not defined TARGET exit /b 22
%TOOLS%\NWGUARD.EXE %TARGET% identify
if errorlevel 1 exit /b 23
%TOOLS%\NWREAD.EXE %TARGET%\S117SRC\install2.swm 62512ee80dafe30ad5bb7db430395eeaa9080473e43c29ea327b5eaf5bfe1963 direct
if errorlevel 1 exit /b 30
echo S124 INSTALL2 POST-REBOOT HASH PASS
exit /b 0
