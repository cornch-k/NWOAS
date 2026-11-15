@echo off
echo S236 B2 REMOVE BADMEMORYLIST
bcdedit /deletevalue {badmemory} badmemorylist
echo DELRC=%ERRORLEVEL%
bcdedit /enum {badmemory}
echo S236 B2 COMPLETE
exit /b 0
