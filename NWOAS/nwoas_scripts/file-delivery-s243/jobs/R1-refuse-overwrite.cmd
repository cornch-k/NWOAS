@echo off
rem S243 R1: the client must refuse to overwrite an existing target (expected exit code 3).
echo S243 R1 REFUSE OVERWRITE
"%~d0\NWGET243.EXE" 1 1965872e848383311793a6383ca289748c42e50b3ff27a2c0798d9a4e70ee580 "C:\NWOAS\drop\gpu-j274\AppleAgxRenderAdmission.sys" 2>&1
echo NWGET243RC=%ERRORLEVEL%
"%~d0\NWGET243.EXE" 1 0000000000000000000000000000000000000000000000000000000000000000 "C:\NWOAS\drop\s243-refuse\x.sys" 2>&1
echo WRONGSHA_RC=%ERRORLEVEL%
if exist "C:\NWOAS\drop\s243-refuse\x.sys.part" echo PART LEFT BEHIND
echo S243 R1 COMPLETE
exit /b 0
