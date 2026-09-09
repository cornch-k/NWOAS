@echo off
echo === S141 DRIVER INVENTORY %date% %time% ===
echo --- PROBLEM DEVICES ---
pnputil /enum-devices /problem /deviceids /drivers
echo --- DISPLAY ---
pnputil /enum-devices /class Display /connected /deviceids /drivers
echo --- USB ---
pnputil /enum-devices /class USB /connected /deviceids /drivers
echo --- NETWORK ---
pnputil /enum-devices /class Net /connected /deviceids /drivers
echo === COMPLETE ===
