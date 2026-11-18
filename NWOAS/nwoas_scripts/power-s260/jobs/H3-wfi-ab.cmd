@echo off
rem S260 H3: same-boot A/B of C-1 (needs NWOAS_PWR_WFI=1 NWOAS_PWR_LIVE=1 at launch): idle with WFI sleep off, then on.
set "T=C:\NWOAS\s260b\NWPWR.EXE"
if not exist "%T%" (echo S260 H3 FAIL NWPWR.EXE missing & exit /b 1)
echo NWSTAT-SET wfi=0
"%T%" idle 3
echo NWSTAT-MARK wfioff-begin
"%T%" idle 30
echo NWSTAT-MARK wfioff-end
"%T%" timer 1000
echo NWSTAT-SET wfi=1
"%T%" idle 3
echo NWSTAT-MARK wfion-begin
"%T%" idle 30
echo NWSTAT-MARK wfion-end
"%T%" timer 1000
echo S260 H3 DONE
exit /b 0
