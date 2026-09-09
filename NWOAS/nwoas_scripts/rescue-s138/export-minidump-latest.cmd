@echo off
setlocal EnableExtensions
set "DUMP="
for /f "delims=" %%F in ('dir /b /a-d /o-d C:\Windows\Minidump\*.dmp 2^>nul') do if not defined DUMP set "DUMP=%%F"
if not defined DUMP (
  echo NWOAS: no minidump found
  exit /b 20
)
echo === NWOAS MINIDUMP %DUMP% BEGIN ===
certutil -encode "C:\Windows\Minidump\%DUMP%" C:\Windows\Temp\NWOAS-LATEST.B64 >nul
if errorlevel 1 exit /b 21
type C:\Windows\Temp\NWOAS-LATEST.B64
del /q C:\Windows\Temp\NWOAS-LATEST.B64
echo === NWOAS MINIDUMP %DUMP% END ===
