@echo off
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; $p=Get-Process -Id 5060; if($p.ProcessName -ne 'Cinebench' -or $p.SessionId -ne 1 -or $p.Path -ne 'C:\NWOAS-BENCH\Cinebench2026\Cinebench.exe'){throw 'Process identity mismatch'}; if($p.CPU -gt 10){throw 'CPU activity changed since diagnosis'}; Stop-Process -InputObject $p -Force -Confirm:$false; Write-Output 'Stopped only diagnosed Cinebench error-dialog process 5060'"
exit /b %errorlevel%
