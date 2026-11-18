@echo off
rem S260 H2: storage baseline S-0. READ-ONLY raw reads of the C: volume (4 KiB random QD1/8/32, 1 MiB seq QD4).
set "T=C:\NWOAS\s260b\NWPWR.EXE"
if not exist "%T%" (echo S260 H2 FAIL NWPWR.EXE missing, run S0b-deliver-nwpwr.cmd & exit /b 1)
echo S260 H2 BEGIN
echo NWSTAT-MARK io-begin
"%T%" io 10
echo NWSTAT-MARK io-end
echo S260 H2 DONE
exit /b 0
