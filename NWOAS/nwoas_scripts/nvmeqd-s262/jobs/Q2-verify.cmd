@echo off
rem S262 Q2: after a restart (or after power-off/on), re-read the Q1 pattern file and check every block.
rem Exit 0 = every block holds the data of its last acknowledged write.
set "Q=C:\NWOAS\s262\NWQD.EXE"
if not exist "%Q%" (echo S262 Q2 FAIL NWQD.EXE missing & exit /b 1)
if not exist C:\NWOAS\s262\integ.bin (echo S262 Q2 FAIL integ.bin missing, run Q1 first & exit /b 1)
echo S262 Q2 BEGIN
echo NWSTAT-MARK qd-verify-begin
"%Q%" verify C:\NWOAS\s262\integ.bin 32
set RC=%ERRORLEVEL%
echo NWSTAT-MARK qd-verify-end
echo S262 Q2 DONE verify_rc=%RC%
exit /b %RC%
