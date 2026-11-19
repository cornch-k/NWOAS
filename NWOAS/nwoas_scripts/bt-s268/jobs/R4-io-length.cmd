@echo off
rem S268 R4 (read only, raw C: reads): the Q1S read workload (NWPWR io, I/O priority 2 through NWIOP) run with
rem 3 s and with 30 s per test.  If the QD32 lat_max grows with the test length while the HV doorbell->CQE
rem maximum (nvmeqd.jsonl lat_ticks_max, marks r4-*) stays small, the multi-second maximum is the NWPWR reap loop
rem (WaitForMultipleObjects serves the smallest signaled slot first), not an I/O held in the HV or the device.
set "P=C:\NWOAS\s260b\NWPWR.EXE"
set "W=C:\NWOAS\s262\NWIOP.EXE"
if not exist "%P%" (echo S268 R4 FAIL NWPWR.EXE missing & exit /b 1)
if not exist "%W%" (echo S268 R4 FAIL NWIOP.EXE missing & exit /b 1)
echo S268 R4 BEGIN
echo NWSTAT-MARK r4-io3-begin
"%W%" 2 "%P%" io 3
set R3=%ERRORLEVEL%
echo NWSTAT-MARK r4-io3-end
echo NWSTAT-MARK r4-io30-begin
"%W%" 2 "%P%" io 30
set R30=%ERRORLEVEL%
echo NWSTAT-MARK r4-io30-end
echo S268 R4 DONE rc3=%R3% rc30=%R30%
exit /b 0
