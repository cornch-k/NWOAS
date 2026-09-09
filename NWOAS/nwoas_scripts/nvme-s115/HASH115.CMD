@echo off
setlocal EnableExtensions EnableDelayedExpansion
reg query HKLM\SYSTEM\CurrentControlSet\Control\MiniNT >nul 2>&1
if errorlevel 1 exit /b 2
if /i not "%PROCESSOR_ARCHITECTURE%"=="ARM64" exit /b 2
if /i not "%SystemDrive%"=="X:" exit /b 2
set "MEDIA=%~d0"
if not exist "%MEDIA%\NWOAS-S115.TAG" exit /b 2
set "TAG="
set /p TAG=<"%MEDIA%\NWOAS-S115.TAG"
if not "%TAG%"=="NWOAS-S115-USB-HASH-20260908" exit /b 2
if exist "%MEDIA%\NWOAS-S115.RUNNING" exit /b 2
ren "%MEDIA%\NWOAS-S115.TAG" NWOAS-S115.RUNNING
if errorlevel 1 exit /b 2
set "OUT=%MEDIA%\NWOAS-SETUP-LOGS\s115-%RANDOM%-%RANDOM%"
set "REPORT=X:\Windows\Temp\S115-%RANDOM%-%RANDOM%.TXT"
echo S115 USB diagnostic running. Five complete file reads; results saved before automatic shutdown.
>"%REPORT%" echo S115 BEGIN %DATE% %TIME%
>>"%REPORT%" echo SELFTEST
"%MEDIA%\SCRIPTS\NWHASH.EXE" "%MEDIA%\SCRIPTS\S106-ABC.TXT" ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad >>"%REPORT%" 2>&1
if errorlevel 1 goto finish
>>"%REPORT%" echo CASE LEGACY BUFFERED
"%MEDIA%\SCRIPTS\NWHASH.EXE" "%MEDIA%\sources\install.swm" 8118bfe1173b8f72161d1eece7eb76b09caa7b30b333f78ae039d390bb04bc8c >>"%REPORT%" 2>&1
>>"%REPORT%" echo CASE_RC !ERRORLEVEL!
>>"%REPORT%" echo CASE ALIGNED BUFFERED 1
"%MEDIA%\SCRIPTS\NWREAD.EXE" "%MEDIA%\sources\install.swm" 8118bfe1173b8f72161d1eece7eb76b09caa7b30b333f78ae039d390bb04bc8c buffered >>"%REPORT%" 2>&1
>>"%REPORT%" echo CASE_RC !ERRORLEVEL!
>>"%REPORT%" echo CASE ALIGNED BUFFERED 2
"%MEDIA%\SCRIPTS\NWREAD.EXE" "%MEDIA%\sources\install.swm" 8118bfe1173b8f72161d1eece7eb76b09caa7b30b333f78ae039d390bb04bc8c buffered >>"%REPORT%" 2>&1
>>"%REPORT%" echo CASE_RC !ERRORLEVEL!
>>"%REPORT%" echo CASE ALIGNED DIRECT 1
"%MEDIA%\SCRIPTS\NWREAD.EXE" "%MEDIA%\sources\install.swm" 8118bfe1173b8f72161d1eece7eb76b09caa7b30b333f78ae039d390bb04bc8c direct >>"%REPORT%" 2>&1
>>"%REPORT%" echo CASE_RC !ERRORLEVEL!
>>"%REPORT%" echo CASE ALIGNED DIRECT 2
"%MEDIA%\SCRIPTS\NWREAD.EXE" "%MEDIA%\sources\install.swm" 8118bfe1173b8f72161d1eece7eb76b09caa7b30b333f78ae039d390bb04bc8c direct >>"%REPORT%" 2>&1
>>"%REPORT%" echo CASE_RC !ERRORLEVEL!
>>"%REPORT%" echo FIVE CASES FINISHED - inspect individual hashes and errors
:finish
>>"%REPORT%" echo S115 END %DATE% %TIME%
type "%REPORT%"
md "%OUT%"
copy /b /y "%REPORT%" "%OUT%\REPORT.TXT"
if errorlevel 1 (
 echo REPORT COPY FAILED. RAM report: %REPORT%. Take a photo before shutdown.
 pause
 exit /b 2
)
ren "%MEDIA%\NWOAS-S115.RUNNING" NWOAS-S115.FINISHED
echo Diagnostic complete. Automatic shutdown in 15 seconds.
ping -n 16 127.0.0.1 >nul
wpeutil shutdown
exit /b 0
