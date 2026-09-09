@echo off
echo === S142 TIMER STABILITY WORKAROUND BEGIN ===
powercfg.exe -attributes SUB_PROCESSOR IDLEDISABLE -ATTRIB_HIDE
powercfg.exe /setacvalueindex SCHEME_CURRENT SUB_PROCESSOR IDLEDISABLE 1
if errorlevel 1 exit /b 21
powercfg.exe /setactive SCHEME_CURRENT
if errorlevel 1 exit /b 22
bcdedit.exe /set {current} disabledynamictick yes
if errorlevel 1 exit /b 23
bcdedit.exe /set {current} useplatformtick yes
if errorlevel 1 exit /b 24
powercfg.exe /query SCHEME_CURRENT SUB_PROCESSOR IDLEDISABLE
bcdedit.exe /enum {current}
echo === S142 TIMER STABILITY WORKAROUND PASS ===
exit /b 0
