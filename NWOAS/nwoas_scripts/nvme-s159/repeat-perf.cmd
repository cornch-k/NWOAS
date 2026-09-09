@echo off
chcp 65001 >nul
echo === S159 REPEATED READ-MOSTLY VALIDATION ===
for /l %%i in (1,1,3) do (
 echo --- SAMPLE %%i ---
 D:\CPUSTRES.EXE
 if errorlevel 1 exit /b 1
 D:\DISKREAD.EXE
 if errorlevel 1 exit /b 2
 timeout /t 3 /nobreak >nul
)
echo === COMPLETE ===
exit /b 0
