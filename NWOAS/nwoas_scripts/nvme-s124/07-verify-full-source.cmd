@echo off
set "TOOLS="
set "TARGET="
for %%d in (C D E F G H I J) do if exist %%d:\NWAGENT.EXE set "TOOLS=%%d:"
for %%d in (C D E F G H I J) do if exist %%d:\S117SRC set "TARGET=%%d:"
if not defined TOOLS exit /b 21
if not defined TARGET exit /b 22
%TOOLS%\NWGUARD.EXE %TARGET% identify
if errorlevel 1 exit /b 23
%TOOLS%\NWREAD.EXE %TARGET%\S117SRC\install.swm 8118bfe1173b8f72161d1eece7eb76b09caa7b30b333f78ae039d390bb04bc8c direct
if errorlevel 1 exit /b 30
%TOOLS%\NWREAD.EXE %TARGET%\S117SRC\install2.swm 62512ee80dafe30ad5bb7db430395eeaa9080473e43c29ea327b5eaf5bfe1963 direct
if errorlevel 1 exit /b 31
>X:\S124-SOURCE-VERIFIED.TAG echo S124 complete source hashes verified this boot
if not exist X:\S124-SOURCE-VERIFIED.TAG exit /b 32
echo S124 BOTH FULL SOURCE HASHES PASS THIS BOOT
exit /b 0
