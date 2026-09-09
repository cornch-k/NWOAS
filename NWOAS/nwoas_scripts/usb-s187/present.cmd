@echo off
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; Get-PnpDevice -PresentOnly | Where-Object {$_.Class -in 'USB','HIDClass','Mouse','Keyboard' -or $_.InstanceId -match 'PNP0D10|XHC|FL1100'} | Select Status,Class,FriendlyName,InstanceId | Format-List"
exit /b %errorlevel%
