@echo off
echo GPUCOV K45a KEEP the 08:10 K43c UMD trace before K43c runs again (K43c deletes C:\NWOAS\gpucov\k43c-umd.log): copy it once to k43c-umd-20261119-0810.log
set D=C:\NWOAS\gpucov
if exist %D%\k43c-umd-20261119-0810.log (echo K45a already kept) else (if exist %D%\k43c-umd.log (copy /y %D%\k43c-umd.log %D%\k43c-umd-20261119-0810.log >nul & echo K45a kept) else (echo K45a no k43c-umd.log))
powershell -NoProfile -Command "Get-ChildItem C:\NWOAS\gpucov\k43c-umd*.log -ErrorAction SilentlyContinue | ForEach-Object { 'K45a FILE '+$_.Name+' bytes='+$_.Length+' written='+$_.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss') }"
echo GPUCOV K45a DONE
exit /b 0
