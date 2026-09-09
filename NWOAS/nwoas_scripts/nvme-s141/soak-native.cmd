@echo off
echo === S141 NATIVE SOAK BEGIN %date% %time% ===
for /l %%I in (1,1,20) do (
  echo --- iteration %%I ---
  D:\CPUSTRES.EXE
  if errorlevel 1 exit /b %errorlevel%
  D:\DISKREAD.EXE
  if errorlevel 1 exit /b %errorlevel%
)
echo === S141 NATIVE SOAK PASS ===
exit /b 0
