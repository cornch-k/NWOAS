@echo off
setx APPLE_AGX_UMD_TRACE_FILE C:\NWOAS\umd-trace.log /M
setx APPLE_AGX_UMD_REFUSALS_ONLY 1 /M
if exist C:\NWOAS\umd-trace.log del C:\NWOAS\umd-trace.log
exit /b 0
