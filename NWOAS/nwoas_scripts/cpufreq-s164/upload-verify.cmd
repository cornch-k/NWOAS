@echo off
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; $b=[Convert]::FromBase64String((Get-Content -Raw -LiteralPath 'C:\NWOAS-S164\PERCPU.B64')); if ($b.Length -ne 4608) { throw 'Length mismatch' }; [IO.File]::WriteAllBytes('C:\NWOAS-S164\PERCPU.NEW',$b); if ((Get-FileHash -Algorithm SHA256 'C:\NWOAS-S164\PERCPU.NEW').Hash -ne 'efdb26b96228f42ae8077a3c0f0194e3fa4acaf2fc89549916cd1771ffcc1da0') { throw 'SHA mismatch' }; Move-Item -Force 'C:\NWOAS-S164\PERCPU.NEW' 'C:\NWOAS-S164\PERCPU.EXE'; Write-Output 'S164 UPLOAD VERIFIED'"
exit /b %errorlevel%
