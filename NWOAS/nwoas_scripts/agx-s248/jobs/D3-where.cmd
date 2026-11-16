@echo off
echo S248 D3 WHO
whoami
echo S248 D3 DIR
dir C:\NWOAS\drop
dir "C:\NWOAS\drop\agx-j274-2742"
echo S248 D3 FSINFO
fsutil fsinfo volumeinfo C: | findstr /i "Serial Name"
mountvol C: /L
exit /b 0
