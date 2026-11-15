@echo off
echo S232 U3 FRESH-BOOT USB COPY OF 3 FILES
set /a N=0
:waitusb
if exist E:\sources\install2.swm goto haveusb
set /a N+=1
if %N% GEQ 60 (echo NO USB E: & exit /b 10)
ping -n 6 127.0.0.1 >nul
goto waitusb
:haveusb
if not exist C:\NWT md C:\NWT
del /q C:\NWT\U3*.* 2>nul
set FAIL=0
copy /b /y E:\sources\install.swm C:\NWT\U3A.swm >nul
D:\NWREAD.EXE C:\NWT\U3A.swm 9b827189c3cbf61fd1c8f2c2bed2888cd3b32113aebf8d375d1ca2c72ba94dfa direct
if errorlevel 1 set FAIL=1
copy /b /y E:\sources\install2.swm C:\NWT\U3B.swm >nul
D:\NWREAD.EXE C:\NWT\U3B.swm c19b48920f6232d4f0cc6d9617d4ba493d6a56182e5c44dcf97b36487982f0f4 direct
if errorlevel 1 set FAIL=1
copy /b /y E:\sources\boot.wim C:\NWT\U3C.wim >nul
D:\NWREAD.EXE C:\NWT\U3C.wim b68afdfd6d8d6ae6ce63186773ca405865482c7ffa6654cd7fd1794584cf325d direct
if errorlevel 1 set FAIL=1
echo S232 U3 RESULT FAIL=%FAIL%
del /q C:\NWT\U3*.* 2>nul
exit /b %FAIL%
