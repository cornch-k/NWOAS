@echo off
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; if(Get-Process -Id 6580 -ErrorAction SilentlyContinue){throw 'Old wrapper still running'}; if(Get-Process -Name Cinebench -ErrorAction SilentlyContinue){throw 'Benchmark already running'}; $p='C:\NWOAS-BENCH\run-user.cmd'; if((Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash -ne 'dfd1cde563fa3849ed6464ab8e49801ae4cd006755390451a21734fe80bfc465'){throw 'Old script mismatch'}; $b=[Convert]::FromBase64String((Get-Content -Raw -LiteralPath 'C:\NWOAS-BENCH\CBRUN-V2.B64')); $tmp='C:\NWOAS-BENCH\run-user-v2.new'; [IO.File]::WriteAllBytes($tmp,$b); if((Get-FileHash -LiteralPath $tmp -Algorithm SHA256).Hash -ne 'a2f2bbaf66bbff94bee6cc39428945bdeef9848b53d0dbbfb04e8ed71b7c93f3'){throw 'New script mismatch'}; [IO.File]::Move($p,'C:\NWOAS-BENCH\run-user-v1.cmd'); [IO.File]::Move($tmp,$p)"
if errorlevel 1 exit /b %errorlevel%
D:\USERCB.EXE
exit /b %errorlevel%
