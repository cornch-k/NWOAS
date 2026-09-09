@echo off
echo === S152 HARDWARE VALIDATION BEGIN %date% %time% ===
echo --- CPU ---
wmic cpu get Name,NumberOfCores,NumberOfLogicalProcessors,CurrentClockSpeed,MaxClockSpeed /format:list
D:\CPUSTRES.EXE
if errorlevel 1 exit /b %errorlevel%
echo --- DISK ---
wmic diskdrive get Index,Model,InterfaceType,Size,Status /format:list
D:\DISKREAD.EXE
if errorlevel 1 exit /b %errorlevel%
echo --- XHC1 AND APPLE USB CONTROLLERS ---
powershell -NoProfile -Command "Get-PnpDevice -PresentOnly:$false | Where-Object { $_.InstanceId -match 'XHC1|APPL8900' -or $_.FriendlyName -match 'xHCI|USB.*Host Controller' } | Sort-Object InstanceId | Format-List Status,Class,FriendlyName,InstanceId,Problem,ProblemStatus"
echo --- PRESENT USB AND HID DEVICES ---
powershell -NoProfile -Command "Get-PnpDevice -PresentOnly | Where-Object { $_.Class -in 'USB','HIDClass' } | Sort-Object Class,FriendlyName | Format-Table -AutoSize Status,Class,FriendlyName,InstanceId"
echo --- RECENT STORAGE USB AND BUGCHECK EVENTS ---
wevtutil qe System /q:"*[System[(EventID=1001 or EventID=129 or EventID=153 or EventID=219)]]" /rd:true /c:12 /f:text
echo === S152 HARDWARE VALIDATION PASS ===
exit /b 0
