@echo off
echo S248 D23 DWMCORE ID
powershell -NoProfile -Command "foreach($f in 'C:\Windows\System32\dwmcore.dll','C:\Windows\System32\d3d11.dll','C:\Windows\System32\dxgi.dll','C:\Windows\System32\ddisplay.dll'){ $b=[IO.File]::ReadAllBytes($f); $pe=[BitConverter]::ToInt32($b,0x3c); $ts=[BitConverter]::ToUInt32($b,$pe+8); $soi=[BitConverter]::ToUInt32($b,$pe+24+56); ''+$f+' TS='+$ts.ToString('X8')+' SOI='+$soi.ToString('X')+' LEN='+$b.Length }"
exit /b 0
