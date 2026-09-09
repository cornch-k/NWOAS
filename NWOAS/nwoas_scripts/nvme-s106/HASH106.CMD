@echo off
setlocal EnableExtensions EnableDelayedExpansion
rem S106: input-file SHA256 only; no format, image apply, GPT write or bcdboot.
reg query HKLM\SYSTEM\CurrentControlSet\Control\MiniNT >nul 2>&1
if errorlevel 1 exit /b 2
if /i not "%PROCESSOR_ARCHITECTURE%"=="ARM64" exit /b 2
set "MEDIA=%~d0"
if not exist "%MEDIA%\NWOAS-S106.TAG" exit /b 2
set "TAG="
set /p TAG=<"%MEDIA%\NWOAS-S106.TAG"
if not "%TAG%"=="NWOAS-S106-HASH-20260908" exit /b 2
if exist "%MEDIA%\NWOAS-S106.RUNNING" exit /b 2
ren "%MEDIA%\NWOAS-S106.TAG" NWOAS-S106.RUNNING
if errorlevel 1 exit /b 2
set "OUT=%MEDIA%\NWOAS-SETUP-LOGS\s106-%RANDOM%-%RANDOM%"
if exist "%OUT%" exit /b 2
md "%OUT%"
>"%OUT%\status.txt" echo S106 BEGIN %DATE% %TIME%
"%MEDIA%\SCRIPTS\NWHASH.EXE" "%MEDIA%\SCRIPTS\S106-ABC.TXT" ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad >"%OUT%\selftest.txt" 2>&1
if errorlevel 1 goto failed
rem Hash USB inputs inside WinPE too, before reading the stored NVMe copy.
"%MEDIA%\SCRIPTS\NWHASH.EXE" "%MEDIA%\sources\install.swm" 8118bfe1173b8f72161d1eece7eb76b09caa7b30b333f78ae039d390bb04bc8c >"%OUT%\usb-first.txt" 2>&1
set "USB1=!ERRORLEVEL!"
"%MEDIA%\SCRIPTS\NWHASH.EXE" "%MEDIA%\sources\install.swm" 8118bfe1173b8f72161d1eece7eb76b09caa7b30b333f78ae039d390bb04bc8c >"%OUT%\usb-second.txt" 2>&1
set "USB2=!ERRORLEVEL!"
>>"%OUT%\status.txt" echo USB_RESULTS first=!USB1! second=!USB2! %TIME%
rem Wait for Windows to enumerate and mount the experimental NTFS partition.
set "DATA="
for /l %%T in (1,1,12) do if not defined DATA (
 for %%D in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do if exist %%D:\t1.swm set "DATA=%%D:"
 if not defined DATA ping -n 6 127.0.0.1 >nul
)
rem If WinPE has left the volume without a letter, find the known offset; assign R only.
if not defined DATA if not exist R:\ (
 for %%N in (0 1 2 3) do if not defined DATA call :mount %%N
)
if not defined DATA (
 >>"%OUT%\status.txt" echo FAILED: t1.swm not mounted; no format or partition change performed
 goto failed
)
>>"%OUT%\status.txt" echo DATA=!DATA! read-only hash
"%MEDIA%\SCRIPTS\NWHASH.EXE" "!DATA!\t1.swm" 8118bfe1173b8f72161d1eece7eb76b09caa7b30b333f78ae039d390bb04bc8c >"%OUT%\nvme-t1-first.txt" 2>&1
set "RC1=!ERRORLEVEL!"
"%MEDIA%\SCRIPTS\NWHASH.EXE" "!DATA!\t1.swm" 8118bfe1173b8f72161d1eece7eb76b09caa7b30b333f78ae039d390bb04bc8c >"%OUT%\nvme-t1-second.txt" 2>&1
set "RC2=!ERRORLEVEL!"
"%MEDIA%\SCRIPTS\NWHASH.EXE" "!DATA!\t2.swm" 62512ee80dafe30ad5bb7db430395eeaa9080473e43c29ea327b5eaf5bfe1963 >"%OUT%\nvme-t2.txt" 2>&1
set "RC3=!ERRORLEVEL!"
>>"%OUT%\status.txt" echo RESULTS t1_first=!RC1! t1_second=!RC2! t2=!RC3! %TIME%
if not "!USB1!!USB2!!RC1!!RC2!!RC3!"=="00000" goto failed
>>"%OUT%\status.txt" echo PASS ALL HASHES
copy /y "%OUT%\status.txt" "%MEDIA%\NWOAS-S106.DONE" >nul
goto finish
:failed
>>"%OUT%\status.txt" echo FAILED OR MISMATCH %TIME%
copy /y "%OUT%\status.txt" "%MEDIA%\NWOAS-S106.FAILED" >nul
:finish
ren "%MEDIA%\NWOAS-S106.RUNNING" NWOAS-S106.FINISHED
exit /b 0
:mount
>"%OUT%\query-%1.txt" echo select disk %1
for /l %%P in (1,1,8) do (
 >>"%OUT%\query-%1.txt" echo select partition %%P
 >>"%OUT%\query-%1.txt" echo detail partition
)
diskpart /s "%OUT%\query-%1.txt">"%OUT%\detail-%1.txt" 2>&1
set "CURP="
set "FOUND="
for /f "usebackq delims=" %%L in ("%OUT%\detail-%1.txt") do (
 set "line=%%L"
 for /f "tokens=1" %%A in ("%%L") do for /l %%P in (1,1,8) do if "%%A"=="%%P" set "CURP=%%P"
 if not "!line:220856320000=!"=="!line!" if defined CURP set "FOUND=!CURP!"
)
if not defined FOUND exit /b 0
(
 echo select disk %1
 echo select partition !FOUND!
 echo assign letter=R
)>"%OUT%\mount-%1.txt"
diskpart /s "%OUT%\mount-%1.txt">"%OUT%\mount-%1.log" 2>&1
if exist R:\t1.swm set "DATA=R:"
exit /b 0
