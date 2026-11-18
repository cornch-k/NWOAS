@echo off
echo E1 BEGIN
wevtutil qe System /q:"*[System[(EventID=1074 or EventID=1076 or EventID=6005 or EventID=6006 or EventID=6008 or EventID=41 or EventID=109 or EventID=20 or EventID=43)]]" /c:14 /rd:true /f:text 2>&1
echo E1 WU
wevtutil qe Microsoft-Windows-WindowsUpdateClient/Operational /c:8 /rd:true /f:text 2>&1
echo E1 END
exit /b 0
