@echo off
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; $b=[Convert]::FromBase64String((Get-Content -Raw -LiteralPath 'C:\NWOAS-S171\MEMTEST.B64')); if($b.Length -ne 5120){throw 'Length mismatch'}; $p='C:\NWOAS-S171\MEMTEST.NEW'; [IO.File]::WriteAllBytes($p,$b); if((Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash -ne 'd632f73fde4294ad7118aa2d4c91f606e0d0537a09020f3d1acff286661ebaab'){throw 'SHA mismatch'}; [IO.File]::Move($p,'C:\NWOAS-S171\MEMTEST.EXE'); Write-Output 'S171 UPLOAD VERIFIED'"
exit /b %errorlevel%
