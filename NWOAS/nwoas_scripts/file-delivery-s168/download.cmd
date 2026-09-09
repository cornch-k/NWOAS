@echo off
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; $d='C:\NWOAS-BENCH'; if(Test-Path -LiteralPath $d){$x=Get-Item -LiteralPath $d -Force; if(-not $x.PSIsContainer -or ($x.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Unexpected destination directory'}}else{New-Item -ItemType Directory -Path $d | Out-Null}; if(Test-Path -LiteralPath ($d+'\CINEBEN.ZIP')){throw 'Final archive already exists'}"
if errorlevel 1 exit /b %errorlevel%
D:\NWGET.EXE
if errorlevel 1 exit /b %errorlevel%
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; $p='C:\NWOAS-BENCH\CINEBEN.ZIP.part'; $x=Get-Item -LiteralPath $p -Force; if($x.Length -ne 783373346 -or ($x.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Archive shape mismatch'}; $h=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash; if($h -ne 'cb6c765f80d53e1fe702de145b6da1c67af5b37d5a399c8f3396a7ecfed78159'){throw 'Archive SHA256 mismatch'}; [IO.File]::Move($p,'C:\NWOAS-BENCH\CINEBEN.ZIP'); Write-Output ('S168 ARCHIVE VERIFIED '+$h)"
exit /b %errorlevel%
