@echo off
rem S262 Q3: remove the 384 MiB pattern file (NWQD.EXE stays).
echo S262 Q3 BEGIN
if exist C:\NWOAS\s262\integ.bin del /f /q C:\NWOAS\s262\integ.bin
if exist C:\NWOAS\s262\integ.bin (echo S262 Q3 FAIL still present & exit /b 1)
echo S262 Q3 DONE
exit /b 0
