@echo off
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; $b=[Convert]::FromBase64String((Get-Content -Raw -LiteralPath 'C:\ProgramData\NWOAS\NWOS177.B64')); $p='C:\ProgramData\NWOAS\NWOS177.EXE'; if(Test-Path -LiteralPath $p){throw 'Existing candidate'}; [IO.File]::WriteAllBytes($p,$b); if((Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash -ne '14a1c914cd54f122a6899d3ae784c290bea641dadc4378883a150abc80db1b7f'){throw 'Worker SHA mismatch'}; Write-Output 'S177 stage verified'"
exit /b %errorlevel%
