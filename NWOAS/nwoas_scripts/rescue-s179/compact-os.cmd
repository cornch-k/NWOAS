@echo off
compact.exe /CompactOS:always
if errorlevel 1 exit /b %errorlevel%
compact.exe /CompactOS:query
fsutil volume diskfree C:
exit /b %errorlevel%
