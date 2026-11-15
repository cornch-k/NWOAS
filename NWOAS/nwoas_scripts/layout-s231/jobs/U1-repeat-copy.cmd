@echo off
echo S231 U1 REPEATED USB COPY TEST (READ-ONLY ON USB)
set /a N=0
:waitusb
if exist E:\sources\install2.swm goto haveusb
set /a N+=1
if %N% GEQ 180 (echo NO USB E: & exit /b 10)
ping -n 6 127.0.0.1 >nul
goto waitusb
:haveusb
if not exist C:\NWT md C:\NWT
for %%i in (1 2 3) do call :one %%i
echo CACHE-ONLY READ: copy to NUL then buffered hash of E:
copy /b E:\sources\install2.swm NUL
D:\NWREAD.EXE E:\sources\install2.swm c19b48920f6232d4f0cc6d9617d4ba493d6a56182e5c44dcf97b36487982f0f4 buffered
echo FC T1 vs GOOD
fc /b C:\NWT\T1.swm C:\S231RC\install2.swm > C:\NWT\fc1.txt
for %%f in (C:\NWT\fc1.txt) do echo FC1 BYTES %%~zf
find /c /v "" C:\NWT\fc1.txt
powershell -NoProfile -Command "Get-Content C:\NWT\fc1.txt -TotalCount 40"
echo S231 U1 COMPLETE
exit /b 0
:one
copy /b /y E:\sources\install2.swm C:\NWT\T%1.swm >nul
D:\NWREAD.EXE C:\NWT\T%1.swm c19b48920f6232d4f0cc6d9617d4ba493d6a56182e5c44dcf97b36487982f0f4 direct
exit /b 0
