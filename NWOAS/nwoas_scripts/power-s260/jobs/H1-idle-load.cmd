@echo off
rem S260 H1: idle / load / timer measurement. Needs S0b-deliver-nwpwr.cmd once (C:\NWOAS\s260b\NWPWR.EXE).
rem NWSTAT-MARK lines make power_module.py snapshot the HV counters (hvstats_report.py prints the tables).
set "T=C:\NWOAS\s260b\NWPWR.EXE"
if not exist "%T%" (echo S260 H1 FAIL NWPWR.EXE missing, run S0b-deliver-nwpwr.cmd & exit /b 1)
echo S260 H1 BEGIN
"%T%" cpuset
echo NWSTAT-MARK idle-begin
"%T%" idle 30
echo NWSTAT-MARK idle-end
echo NWSTAT-MARK load8-begin
"%T%" load 20 8
echo NWSTAT-MARK load8-end
echo NWSTAT-MARK load4-begin
"%T%" load 20 4
echo NWSTAT-MARK load4-end
echo NWSTAT-MARK timer-begin
"%T%" timer 2000
echo NWSTAT-MARK timer-end
echo S260 H1 DONE
exit /b 0
