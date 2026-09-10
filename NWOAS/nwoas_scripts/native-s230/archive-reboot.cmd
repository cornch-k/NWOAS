@echo off
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; $p='C:\ProgramData\NWOAS\IO195.DAT'; $d='C:\ProgramData\NWOAS\IO195-s230-first.DAT'; if((Get-Item -LiteralPath $p).Length -ne 268435456){throw 'Unexpected test-file size'}; if(Test-Path -LiteralPath $d){throw 'Archive already exists'}; if((Get-FileHash -LiteralPath $p).Hash -ne 'EB11897202F621134A7EC3C737C4AC3FBEEE61EF69FAA50D4B9469D8998538D6'){throw 'Test-file checksum mismatch'}; Move-Item -LiteralPath $p -Destination $d; Write-Output 'S230 test archive hash verified and renamed'"
if errorlevel 1 exit /b %errorlevel%
echo S230 clean restart
shutdown.exe /r /t 0
