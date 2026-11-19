@echo off
rem S262S Q1S: the same workload as Q1 (S260 H2 raw C: reads + NWQD integ), but NWPWR and NWQD are
rem started through NWIOP.EXE at I/O priority 2 (normal).  The agent task runs at Task Scheduler
rem priority 7 (IoPriorityLow); classpnp then keeps at most one low-priority I/O outstanding.
rem Marks are qdn-* so the same boot can run Q1 (low) and Q1S (normal) for an A/B.
rem Needs power-s260 S0b (NWPWR.EXE), Q0 (NWQD.EXE) and Q0S (NWIOP.EXE) once.
set "P=C:\NWOAS\s260b\NWPWR.EXE"
set "Q=C:\NWOAS\s262\NWQD.EXE"
set "W=C:\NWOAS\s262\NWIOP.EXE"
if not exist "%P%" (echo S262 Q1S FAIL NWPWR.EXE missing, run power-s260 S0b-deliver-nwpwr.cmd & exit /b 1)
if not exist "%Q%" (echo S262 Q1S FAIL NWQD.EXE missing, run Q0-deliver-nwqd.cmd & exit /b 1)
if not exist "%W%" (echo S262 Q1S FAIL NWIOP.EXE missing, run Q0S-deliver-nwiop.cmd & exit /b 1)
set /a SEED=%RANDOM%*32768+%RANDOM%+1
echo S262 Q1S BEGIN seed=%SEED%
echo NWSTAT-MARK qdn-io-begin
"%W%" 2 "%P%" io 10
set PRC=%ERRORLEVEL%
echo NWSTAT-MARK qdn-io-end
echo NWSTAT-MARK qdn-integ-begin
"%W%" 2 "%Q%" integ C:\NWOAS\s262\integ.bin 384 32 %SEED%
set RC=%ERRORLEVEL%
echo NWSTAT-MARK qdn-integ-end
echo S262 Q1S DONE io_rc=%PRC% integ_rc=%RC%
if not %PRC%==0 exit /b 9%PRC%
exit /b %RC%
