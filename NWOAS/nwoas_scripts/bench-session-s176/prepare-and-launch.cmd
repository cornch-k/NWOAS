@echo off
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; $s='D:\CBRUN.CMD'; $d='C:\NWOAS-BENCH\run-user.cmd'; if(Test-Path -LiteralPath $d){throw 'Launch script already exists'}; if((Get-FileHash -LiteralPath $s -Algorithm SHA256).Hash -ne 'dfd1cde563fa3849ed6464ab8e49801ae4cd006755390451a21734fe80bfc465'){throw 'Script hash mismatch'}; Copy-Item -LiteralPath $s -Destination $d; if((Get-FileHash -LiteralPath $d -Algorithm SHA256).Hash -ne 'dfd1cde563fa3849ed6464ab8e49801ae4cd006755390451a21734fe80bfc465'){throw 'Copied script hash mismatch'}"
if errorlevel 1 exit /b %errorlevel%
D:\USERCB.EXE
exit /b %errorlevel%
