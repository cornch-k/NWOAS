@echo off
set "TOOLS="
set "TARGET="
for %%d in (C D E F G H I J) do if exist %%d:\NWAGENT.EXE set "TOOLS=%%d:"
for %%d in (C D E F G H I J) do if exist %%d:\S117SRC set "TARGET=%%d:"
if not defined TOOLS exit /b 21
if not defined TARGET exit /b 22
%TOOLS%\NWGUARD.EXE %TARGET% identify
if errorlevel 1 exit /b 23
if exist %TARGET%\NWOAS124 exit /b 24
md %TARGET%\NWOAS124
if errorlevel 1 exit /b 25
md %TARGET%\NWOAS124\B
if errorlevel 1 exit /b 26
md %TARGET%\NWOAS124\U
if errorlevel 1 exit /b 27
echo S124 BUFFERED COPY
copy /b /y %TOOLS%\P124.BIN %TARGET%\NWOAS124\B\P124.BIN
if errorlevel 1 exit /b 28
echo S124 DIRECT COPY
xcopy /j /y %TOOLS%\P124.BIN %TARGET%\NWOAS124\U\
if errorlevel 1 exit /b 29
%TOOLS%\NWREAD.EXE %TARGET%\NWOAS124\B\P124.BIN 0d2d719bb5499065d7d96dfadbf137f92f5e7411e6ae511093cf3af2176c2023 direct
if errorlevel 1 exit /b 30
%TOOLS%\NWREAD.EXE %TARGET%\NWOAS124\B\P124.BIN 0d2d719bb5499065d7d96dfadbf137f92f5e7411e6ae511093cf3af2176c2023 buffered
if errorlevel 1 exit /b 30
%TOOLS%\NWREAD.EXE %TARGET%\NWOAS124\U\P124.BIN 0d2d719bb5499065d7d96dfadbf137f92f5e7411e6ae511093cf3af2176c2023 direct
if errorlevel 1 exit /b 30
%TOOLS%\NWREAD.EXE %TARGET%\NWOAS124\U\P124.BIN 0d2d719bb5499065d7d96dfadbf137f92f5e7411e6ae511093cf3af2176c2023 buffered
if errorlevel 1 exit /b 30
%TOOLS%\NWVFLUSH.EXE %TARGET% flush
if errorlevel 1 exit /b 31
echo S124 PRE-REBOOT BOTH COPIES EXACT SIZE AND SHA256 PASS
exit /b 0
