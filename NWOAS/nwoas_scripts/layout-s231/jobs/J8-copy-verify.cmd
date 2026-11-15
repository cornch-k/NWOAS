@echo off
echo S231 J8 COPY USB SOURCE TO SSD AND VERIFY
set /a N=0
:wait
if exist E:\sources\install2.swm goto ready
set /a N+=1
if %N% GEQ 120 exit /b 81
ping -n 6 127.0.0.1 >nul
goto wait
:ready
vol E:
if not exist C:\S231SRC md C:\S231SRC
copy /b /y E:\sources\install.swm C:\S231SRC\install.swm
if errorlevel 1 exit /b 82
copy /b /y E:\sources\install2.swm C:\S231SRC\install2.swm
if errorlevel 1 exit /b 83
dir C:\S231SRC
echo VERIFY DIRECT install.swm
D:\NWREAD.EXE C:\S231SRC\install.swm 9b827189c3cbf61fd1c8f2c2bed2888cd3b32113aebf8d375d1ca2c72ba94dfa direct
if errorlevel 1 exit /b 84
echo VERIFY DIRECT install2.swm
D:\NWREAD.EXE C:\S231SRC\install2.swm c19b48920f6232d4f0cc6d9617d4ba493d6a56182e5c44dcf97b36487982f0f4 direct
if errorlevel 1 exit /b 85
echo VERIFY BUFFERED install.swm
D:\NWREAD.EXE C:\S231SRC\install.swm 9b827189c3cbf61fd1c8f2c2bed2888cd3b32113aebf8d375d1ca2c72ba94dfa buffered
if errorlevel 1 exit /b 86
echo VERIFY BUFFERED install2.swm
D:\NWREAD.EXE C:\S231SRC\install2.swm c19b48920f6232d4f0cc6d9617d4ba493d6a56182e5c44dcf97b36487982f0f4 buffered
if errorlevel 1 exit /b 87
echo S231 J8 PASS SOURCE ON SSD MATCHES HOST-VERIFIED USB CONTENT
exit /b 0
