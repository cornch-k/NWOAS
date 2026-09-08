@echo off
set "TOOLS="
set "TARGET="
for %%d in (C D E F G H I J) do if exist %%d:\NWAGENT.EXE set "TOOLS=%%d:"
for %%d in (C D E F G H I J) do if exist %%d:\S117SRC set "TARGET=%%d:"
if not defined TOOLS exit /b 21
if not defined TARGET exit /b 22
%TOOLS%\NWGUARD.EXE %TARGET% identify
if errorlevel 1 exit /b 23
%TOOLS%\NWREAD.EXE %TARGET%\NWOAS124\B\P124.BIN 0d2d719bb5499065d7d96dfadbf137f92f5e7411e6ae511093cf3af2176c2023 direct
if errorlevel 1 exit /b 30
%TOOLS%\NWREAD.EXE %TARGET%\NWOAS124\B\P124.BIN 0d2d719bb5499065d7d96dfadbf137f92f5e7411e6ae511093cf3af2176c2023 buffered
if errorlevel 1 exit /b 30
%TOOLS%\NWREAD.EXE %TARGET%\NWOAS124\U\P124.BIN 0d2d719bb5499065d7d96dfadbf137f92f5e7411e6ae511093cf3af2176c2023 direct
if errorlevel 1 exit /b 30
%TOOLS%\NWREAD.EXE %TARGET%\NWOAS124\U\P124.BIN 0d2d719bb5499065d7d96dfadbf137f92f5e7411e6ae511093cf3af2176c2023 buffered
if errorlevel 1 exit /b 30
echo S124 POST-REBOOT BOTH COPIES SHA256 PASS
exit /b 0
