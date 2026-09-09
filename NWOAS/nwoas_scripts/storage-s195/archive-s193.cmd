@echo off
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; $p='C:\ProgramData\NWOAS\IO195.DAT'; $d='C:\ProgramData\NWOAS\IO195-s193-gap50.DAT'; $f=Get-Item -LiteralPath $p; if($f.Length -ne 268435456){throw 'S195 unexpected size'}; if(Test-Path $d){throw 'S195 archive exists'}; Get-FileHash -LiteralPath $p | Format-List; Move-Item -LiteralPath $p -Destination $d; Get-Item -LiteralPath $d | Select Name,Length | Format-List"
exit /b %errorlevel%
