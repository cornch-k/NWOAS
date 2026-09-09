@echo off
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; $p='C:\ProgramData\NWOAS\IO195.DAT'; $d='C:\ProgramData\NWOAS\IO195-s200-gap50.DAT'; $f=Get-Item -LiteralPath $p; if($f.Length -ne 268435456){throw 'S195 unexpected size'}; if(Test-Path $d){throw 'S195 archive exists'}; $h=Get-FileHash -LiteralPath $p; $h | Format-List; if($h.Hash -ne 'EB11897202F621134A7EC3C737C4AC3FBEEE61EF69FAA50D4B9469D8998538D6'){throw 'pattern hash mismatch'}; Move-Item -LiteralPath $p -Destination $d; Get-Item -LiteralPath $d | Select Name,Length | Format-List"
exit /b %errorlevel%
