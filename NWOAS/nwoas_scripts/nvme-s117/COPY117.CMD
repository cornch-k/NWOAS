@echo off
setlocal EnableExtensions EnableDelayedExpansion
rem S117: xcopy /J to new NVMe directory, then direct and buffered SHA256.
rem Existing files are preserved. No format, GPT changes, DISM or bcdboot.
reg query HKLM\SYSTEM\CurrentControlSet\Control\MiniNT >nul 2>&1
if errorlevel 1 exit /b 2
if /i not "%PROCESSOR_ARCHITECTURE%"=="ARM64" exit /b 2
if /i not "%SystemDrive%"=="X:" exit /b 2
set "MEDIA=%~d0"
if not exist "%MEDIA%\NWOAS-S117.TAG" exit /b 2
set "TAG="
set /p TAG=<"%MEDIA%\NWOAS-S117.TAG"
if not "%TAG%"=="NWOAS-S117-DIRECT-COPY-20260908" exit /b 2
if exist "%MEDIA%\NWOAS-S117.RUNNING" exit /b 2
ren "%MEDIA%\NWOAS-S117.TAG" NWOAS-S117.RUNNING
if errorlevel 1 exit /b 2
set "OUT=%MEDIA%\NWOAS-SETUP-LOGS\s117-%RANDOM%-%RANDOM%"
set "REPORT=X:\Windows\Temp\S117-%RANDOM%-%RANDOM%.TXT"
set "HASH1=8118bfe1173b8f72161d1eece7eb76b09caa7b30b333f78ae039d390bb04bc8c"
set "HASH2=62512ee80dafe30ad5bb7db430395eeaa9080473e43c29ea327b5eaf5bfe1963"
>"%REPORT%" echo S117 BEGIN %DATE% %TIME%
echo S117: identifying SSD, copying without buffering, and verifying both images.
set "DATA="
set /a FOUND=0
for %%D in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do if /i not "%%D:"=="%MEDIA%" if exist "%%D:\t1.swm" (
 for %%I in ("%%D:\t1.swm") do if "%%~zI"=="3987720636" (
  set "DATA=%%D:"
  set /a FOUND+=1
 )
)
if not "!FOUND!"=="1" (
 >>"%REPORT%" echo FAIL target candidate count !FOUND!; no writes to internal SSD
 goto finish
)
>>"%REPORT%" echo TARGET !DATA! identity check against preserved S106-S112 file
"%MEDIA%\SCRIPTS\NWREAD.EXE" "!DATA!\t1.swm" e3734bc93c439dc28a0a695b5cc6f26f7d4f2fc6041ee5298b06f60e9ad880a1 direct >>"%REPORT%" 2>&1
if errorlevel 1 (
 >>"%REPORT%" echo FAIL target fingerprint; no writes to internal SSD
 goto finish
)
dir /a "!DATA!\" >>"%REPORT%" 2>&1
fsutil volume diskfree !DATA! >>"%REPORT%" 2>&1
set "DEST=!DATA!\S117SRC"
if exist "!DEST!" (
 >>"%REPORT%" echo FAIL destination already exists; preserved for inspection
 goto finish
)
md "!DEST!"
if errorlevel 1 goto failed
>>"%REPORT%" echo COPY install.swm using xcopy /J
xcopy "%MEDIA%\sources\install.swm" "!DEST!\" /J /Y >>"%REPORT%" 2>&1
set "RC=!ERRORLEVEL!"
>>"%REPORT%" echo COPY1_RC !RC!
if not "!RC!"=="0" goto failed
>>"%REPORT%" echo COPY install2.swm using xcopy /J
xcopy "%MEDIA%\sources\install2.swm" "!DEST!\" /J /Y >>"%REPORT%" 2>&1
set "RC=!ERRORLEVEL!"
>>"%REPORT%" echo COPY2_RC !RC!
if not "!RC!"=="0" goto failed
set "FAILED=0"
for %%M in (direct buffered) do (
 >>"%REPORT%" echo VERIFY install.swm %%M
 "%MEDIA%\SCRIPTS\NWREAD.EXE" "!DEST!\install.swm" !HASH1! %%M >>"%REPORT%" 2>&1
 set "RC=!ERRORLEVEL!"
 >>"%REPORT%" echo VERIFY1_%%M_RC !RC!
 if not "!RC!"=="0" set "FAILED=1"
 >>"%REPORT%" echo VERIFY install2.swm %%M
 "%MEDIA%\SCRIPTS\NWREAD.EXE" "!DEST!\install2.swm" !HASH2! %%M >>"%REPORT%" 2>&1
 set "RC=!ERRORLEVEL!"
 >>"%REPORT%" echo VERIFY2_%%M_RC !RC!
 if not "!RC!"=="0" set "FAILED=1"
)
if "!FAILED!"=="1" goto failed
>>"%REPORT%" echo PASS S117 BOTH COPIES MATCH ORIGINAL IN BOTH READ MODES
>"!DEST!\VERIFIED-S117.TXT" echo S117 SHA256 verified in direct and buffered modes.
goto finish
:failed
>>"%REPORT%" echo FAIL S117 COPY OR VERIFICATION; partial copies preserved
:finish
if defined DATA dir /a "!DATA!\" >>"%REPORT%" 2>&1
>>"%REPORT%" echo S117 END %DATE% %TIME%
type "%REPORT%"
if exist "%OUT%" (
 echo Report destination exists; RAM report preserved at %REPORT%
 pause
 exit /b 2
)
md "%OUT%"
copy /b /y "%REPORT%" "%OUT%\REPORT.TXT"
if errorlevel 1 (
 echo REPORT COPY FAILED. RAM report: %REPORT%
 pause
 exit /b 2
)
ren "%MEDIA%\NWOAS-S117.RUNNING" NWOAS-S117.FINISHED
echo Test complete. Inspect report for PASS or FAIL. Shutdown in 15 seconds.
ping -n 16 127.0.0.1 >nul
wpeutil shutdown
exit /b 0
