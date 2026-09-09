@echo off
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; $s=[Diagnostics.Stopwatch]::StartNew(); $n=0; while($s.Elapsed.TotalSeconds -lt 300){ $n++; & D:\CPUSTRES.EXE; if($LASTEXITCODE -ne 0){exit 11}; if(($n -band 15) -eq 0){ & D:\DISKREAD.EXE; if($LASTEXITCODE -ne 0){exit 12} } }; Write-Output ('S164 CPU SOAK PASS samples='+$n+' elapsed_ms='+$s.ElapsedMilliseconds)"
exit /b %errorlevel%
