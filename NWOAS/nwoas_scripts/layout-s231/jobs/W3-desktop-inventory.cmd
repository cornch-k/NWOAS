@echo off
echo S231 W3 DESKTOP READ-ONLY INVENTORY
ver
echo CPUS %NUMBER_OF_PROCESSORS%
systeminfo
pnputil /enum-devices /problem
echo S231 W3 COMPLETE
exit /b 0
