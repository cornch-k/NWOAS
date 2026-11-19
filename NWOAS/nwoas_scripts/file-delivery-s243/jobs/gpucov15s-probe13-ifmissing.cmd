@echo off
echo S243 DELIVER BEGIN 2 file(s) to C:\NWOAS\gpucov (gpucov15s: skip a file already present with the same SHA-256; NWGET243 refuses to overwrite)
set "T=C:\NWOAS\gpucov"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $p=$env:T; while($p){ if(Test-Path -LiteralPath $p){ $x=Get-Item -LiteralPath $p -Force; if(-not $x.PSIsContainer -or ($x.Attributes -band [IO.FileAttributes]::ReparsePoint)){ throw ('unsafe path component '+$p) } }; $p=Split-Path -Parent $p }; New-Item -ItemType Directory -Force -Path $env:T | Out-Null; 'S243 DIR READY '+$env:T" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL dir & exit /b 1)
if exist "%T%\nwgpuprobe13-arm64.exe" (echo S243 SKIP present nwgpuprobe13-arm64.exe) else ("%~d0\NWGET243.EXE" 0 43ed4e400fbe6c516f9de60ad1e844ca44ab40d012fdc3e218329e70d206e530 "%T%\nwgpuprobe13-arm64.exe" 2>&1)
if errorlevel 1 (echo S243 DELIVER FAIL index 0 & exit /b 1)
if exist "%T%\nwgpuprobe13-x64.exe" (echo S243 SKIP present nwgpuprobe13-x64.exe) else ("%~d0\NWGET243.EXE" 1 93f65771b0aa26eeef18988c92f42fbd1ead01c3ba36239d16c8fadca56b393e "%T%\nwgpuprobe13-x64.exe" 2>&1)
if errorlevel 1 (echo S243 DELIVER FAIL index 1 & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('nwgpuprobe13-arm64.exe','nwgpuprobe13-x64.exe'); $h=@('43ed4e400fbe6c516f9de60ad1e844ca44ab40d012fdc3e218329e70d206e530','93f65771b0aa26eeef18988c92f42fbd1ead01c3ba36239d16c8fadca56b393e'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $env:T $n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); $l=(Get-Item -LiteralPath $f).Length; if($g -eq $h[$i]){ 'S243 READBACK MATCH '+$n[$i]+' '+$l+' '+$g } else { 'S243 READBACK MISMATCH '+$n[$i]+' '+$l+' '+$g; $bad=1 } }; exit $bad" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL readback & exit /b 1)
echo S243 DELIVER COMPLETE
exit /b 0
