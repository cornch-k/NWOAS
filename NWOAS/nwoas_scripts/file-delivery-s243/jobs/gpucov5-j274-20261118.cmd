@echo off
echo S243 DELIVER BEGIN 5 file(s) to C:\NWOAS\gpucov
set "T=C:\NWOAS\gpucov"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $p=$env:T; while($p){ if(Test-Path -LiteralPath $p){ $x=Get-Item -LiteralPath $p -Force; if(-not $x.PSIsContainer -or ($x.Attributes -band [IO.FileAttributes]::ReparsePoint)){ throw ('unsafe path component '+$p) } }; $p=Split-Path -Parent $p }; New-Item -ItemType Directory -Force -Path $env:T | Out-Null; 'S243 DIR READY '+$env:T" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL dir & exit /b 1)
"%~d0\NWGET243.EXE" 0 6796b8d625253f82411c38553572ad549fa1b43caa3dd5efdf55601b2e56667f "%T%\dxgi-trace-proxy-arm64.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 0 & exit /b 1)
"%~d0\NWGET243.EXE" 1 28c3392315c1e278953e5728aa71e792506ac0b0daaffc9cfb7e9161fc6031d7 "%T%\d3d11-warp-proxy-arm64.dll" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 1 & exit /b 1)
"%~d0\NWGET243.EXE" 2 695681b8bb512be50ae84f88fbaa621fb4e6b51fe490145cfda8615b982edea4 "%T%\cb26ui2.ps1" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 2 & exit /b 1)
"%~d0\NWGET243.EXE" 3 eaed2df3bc00f392ca4cbd6125af571b4c8ef8c86391edbca24d3a89d019dc16 "%T%\wintree.ps1" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 3 & exit /b 1)
"%~d0\NWGET243.EXE" 4 89b4cb8050e7b8bbb5e3990543e0cdb9d42bffe49a63a4681f18c6aa2d833b8a "%T%\edge2.ps1" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL index 4 & exit /b 1)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$n=@('dxgi-trace-proxy-arm64.dll','d3d11-warp-proxy-arm64.dll','cb26ui2.ps1','wintree.ps1','edge2.ps1'); $h=@('6796b8d625253f82411c38553572ad549fa1b43caa3dd5efdf55601b2e56667f','28c3392315c1e278953e5728aa71e792506ac0b0daaffc9cfb7e9161fc6031d7','695681b8bb512be50ae84f88fbaa621fb4e6b51fe490145cfda8615b982edea4','eaed2df3bc00f392ca4cbd6125af571b4c8ef8c86391edbca24d3a89d019dc16','89b4cb8050e7b8bbb5e3990543e0cdb9d42bffe49a63a4681f18c6aa2d833b8a'); $bad=0; for($i=0;$i -lt $n.Count;$i++){ $f=Join-Path $env:T $n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); $l=(Get-Item -LiteralPath $f).Length; if($g -eq $h[$i]){ 'S243 READBACK MATCH '+$n[$i]+' '+$l+' '+$g } else { 'S243 READBACK MISMATCH '+$n[$i]+' '+$l+' '+$g; $bad=1 } }; exit $bad" 2>&1
if errorlevel 1 (echo S243 DELIVER FAIL readback & exit /b 1)
echo S243 DELIVER COMPLETE
exit /b 0
