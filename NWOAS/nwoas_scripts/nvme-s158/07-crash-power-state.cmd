@echo off
echo === S158 crash and power state ===
dir /a-d /o-d C:\Windows\Minidump\*.dmp
wevtutil qe System /q:"*[System[(EventID=1001 or EventID=41 or EventID=6008 or EventID=161 or EventID=46 or EventID=42 or EventID=107)]]" /rd:true /c:12 /f:text
powercfg /a
powercfg /query SCHEME_CURRENT SUB_SLEEP
powercfg /query SCHEME_CURRENT SUB_VIDEO
exit /b 0
