@echo off
rem S262 Q1: storage A/B under the concurrent NVMe path (or the old path, same job).
rem 1) the S260 H2 read workload (raw C: reads, 4 KiB random QD1/8/32, 1 MiB seq QD4, 10 s each)
rem 2) NWQD integ: 384 MiB pattern file on C:, seq write, flush, random 4 KiB overwrites (QD1 then QD32,
rem    half write-through = FUA), flush, random + sequential read-back of every block, header, flush.
rem Needs power-s260 S0b (C:\NWOAS\s260b\NWPWR.EXE) and Q0 (C:\NWOAS\s262\NWQD.EXE) once.
set "P=C:\NWOAS\s260b\NWPWR.EXE"
set "Q=C:\NWOAS\s262\NWQD.EXE"
if not exist "%P%" (echo S262 Q1 FAIL NWPWR.EXE missing, run power-s260 S0b-deliver-nwpwr.cmd & exit /b 1)
if not exist "%Q%" (echo S262 Q1 FAIL NWQD.EXE missing, run Q0-deliver-nwqd.cmd & exit /b 1)
set /a SEED=%RANDOM%*32768+%RANDOM%+1
echo S262 Q1 BEGIN seed=%SEED%
echo NWSTAT-MARK qd-io-begin
"%P%" io 10
echo NWSTAT-MARK qd-io-end
echo NWSTAT-MARK qd-integ-begin
"%Q%" integ C:\NWOAS\s262\integ.bin 384 32 %SEED%
set RC=%ERRORLEVEL%
echo NWSTAT-MARK qd-integ-end
echo S262 Q1 DONE integ_rc=%RC%
exit /b %RC%
