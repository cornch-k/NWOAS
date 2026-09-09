@echo off
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; if((Get-Item -Force 'C:\S117SRC').Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Source directory is a reparse point'}; $f=Get-Item -LiteralPath 'C:\S117SRC\install.swm'; if($f.Length -ne 3987720636 -or ($f.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Source shape mismatch'}; $s=[IO.File]::OpenRead('C:\S117SRC\install.swm'); try { $o=[Console]::OpenStandardOutput(); $s.CopyTo($o,65536); $o.Flush() } finally { $s.Dispose() }"
exit /b %errorlevel%
