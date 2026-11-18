@echo off
rem S260 H4: same-boot bisect of C-1 (wfi) x C-2 (sectick): load8, load8 with QoS opt-out (hi), io.
rem Launch with NWOAS_S260_HV=1 NWOAS_PWR_WFI=1 NWOAS_PWR_SECTICK=100 NWOAS_PWR_LIVE=1 (NWOAS_S260_MADT 0 or 1).
rem Needs S0b-deliver-nwpwr.cmd once (C:\NWOAS\s260b\NWPWR.EXE).
set "T=C:\NWOAS\s260b\NWPWR.EXE"
if not exist "%T%" (echo S260 H4 FAIL NWPWR.EXE missing, run S0b-deliver-nwpwr.cmd & exit /b 1)
echo S260 H4 BEGIN
"%T%" cpuset
powercfg /q SCHEME_CURRENT SUB_PROCESSOR CPMINCORES1 2>&1 | findstr /i "0x"
powercfg /q SCHEME_CURRENT SUB_PROCESSOR SCHEDPOLICY 2>&1 | findstr /i "0x"
echo NWSTAT-SET wfi=0 sectick=0
echo S260 H4 PHASE w0s0
"%T%" idle 3
echo NWSTAT-MARK w0s0-load8-begin
"%T%" load 20 8
echo NWSTAT-MARK w0s0-load8-end
echo NWSTAT-MARK w0s0-load8hi-begin
"%T%" load 20 8 hi
echo NWSTAT-MARK w0s0-load8hi-end
echo NWSTAT-MARK w0s0-io-begin
"%T%" io 5
echo NWSTAT-MARK w0s0-io-end
echo NWSTAT-SET wfi=1 sectick=0
echo S260 H4 PHASE w1s0
"%T%" idle 3
echo NWSTAT-MARK w1s0-load8-begin
"%T%" load 20 8
echo NWSTAT-MARK w1s0-load8-end
echo NWSTAT-MARK w1s0-load8hi-begin
"%T%" load 20 8 hi
echo NWSTAT-MARK w1s0-load8hi-end
echo NWSTAT-MARK w1s0-io-begin
"%T%" io 5
echo NWSTAT-MARK w1s0-io-end
echo NWSTAT-SET wfi=1 sectick=1
echo S260 H4 PHASE w1s1
"%T%" idle 3
echo NWSTAT-MARK w1s1-load8-begin
"%T%" load 20 8
echo NWSTAT-MARK w1s1-load8-end
echo NWSTAT-MARK w1s1-load8hi-begin
"%T%" load 20 8 hi
echo NWSTAT-MARK w1s1-load8hi-end
echo NWSTAT-MARK w1s1-io-begin
"%T%" io 5
echo NWSTAT-MARK w1s1-io-end
echo NWSTAT-SET wfi=0 sectick=1
echo S260 H4 PHASE w0s1
"%T%" idle 3
echo NWSTAT-MARK w0s1-load8-begin
"%T%" load 20 8
echo NWSTAT-MARK w0s1-load8-end
echo NWSTAT-MARK w0s1-load8hi-begin
"%T%" load 20 8 hi
echo NWSTAT-MARK w0s1-load8hi-end
echo NWSTAT-MARK w0s1-io-begin
"%T%" io 5
echo NWSTAT-MARK w0s1-io-end
echo NWSTAT-SET wfi=1 sectick=1
echo S260 H4 DONE
exit /b 0
