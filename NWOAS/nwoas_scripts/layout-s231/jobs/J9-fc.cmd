@echo off
echo S231 J9 CHARACTERIZE SSD COPY MISMATCH (READ-ONLY EXCEPT REPORT FILE)
echo REREAD DIRECT
D:\NWREAD.EXE C:\S231SRC\install.swm 9b827189c3cbf61fd1c8f2c2bed2888cd3b32113aebf8d375d1ca2c72ba94dfa direct
echo REREAD BUFFERED
D:\NWREAD.EXE C:\S231SRC\install.swm 9b827189c3cbf61fd1c8f2c2bed2888cd3b32113aebf8d375d1ca2c72ba94dfa buffered
if exist C:\S231FC.TXT del C:\S231FC.TXT
fc.exe /b E:\sources\install.swm C:\S231SRC\install.swm >C:\S231FC.TXT
echo FC EXIT %errorlevel%
dir C:\S231FC.TXT
set /a K=0
for /f "usebackq delims=" %%l in ("C:\S231FC.TXT") do call :show "%%l"
echo S231 J9 COMPLETE
exit /b 0
:show
set /a K+=1
if %K% LEQ 400 echo %~1
exit /b 0
