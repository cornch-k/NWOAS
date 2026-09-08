@echo off
echo S124 DISM ERRORS
if exist X:\S124-DISM.LOG find /i "error" X:\S124-DISM.LOG
echo S124 CLEANUP ERRORS
if exist X:\S124-REMOVE.LOG type X:\S124-REMOVE.LOG
echo S124 TARGET FILES
for %%d in (C D E F G H I J) do if exist %%d:\S124-APPLY-STARTED.TXT dir %%d:\
echo S124 END DIAGNOSTICS
exit /b 0
