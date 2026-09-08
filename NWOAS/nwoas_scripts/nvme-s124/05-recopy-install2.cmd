@echo off
set "TOOLS="
set "TARGET="
for %%d in (C D E F G H I J) do if exist %%d:\NWAGENT.EXE set "TOOLS=%%d:"
for %%d in (C D E F G H I J) do if exist %%d:\S117SRC set "TARGET=%%d:"
if not defined TOOLS exit /b 21
if not defined TARGET exit /b 22
%TOOLS%\NWGUARD.EXE %TARGET% identify
if errorlevel 1 exit /b 23
set "USB="
for %%d in (C D E F G H I J) do if exist %%d:\sources\install2.swm if exist %%d:\NWOAS-S122.RUNNING set "USB=%%d:"
if not defined USB exit /b 24
if not "%TARGET%"=="C:" exit /b 25
echo S124 RECOPY INSTALL2 USING DIRECT USB INPUT
dir %TARGET%\S117SRC\install2.swm
xcopy /j /y %USB%\sources\install2.swm %TARGET%\S117SRC\
if errorlevel 1 exit /b 26
%TOOLS%\NWTRIM.EXE C:\S117SRC\install2.swm 62512ee80dafe30ad5bb7db430395eeaa9080473e43c29ea327b5eaf5bfe1963 direct
if errorlevel 1 exit /b 27
%TOOLS%\NWREAD.EXE C:\S117SRC\install2.swm 62512ee80dafe30ad5bb7db430395eeaa9080473e43c29ea327b5eaf5bfe1963 direct
if errorlevel 1 exit /b 28
%TOOLS%\NWVFLUSH.EXE %TARGET% flush
if errorlevel 1 exit /b 29
echo S124 INSTALL2 RECOPY EXACT LENGTH HASH FLUSH PASS
exit /b 0
