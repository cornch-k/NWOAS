@echo off
echo GPUCOV K7 READ (result files only, nothing is run)
powershell -NoProfile -Command "$d='C:\NWOAS\gpucov'; Get-ChildItem $d -File | Where-Object { $_.Extension -in '.txt','.log','.png','.html' } | ForEach-Object { 'K7 FILE '+$_.Name+' '+$_.Length+' '+$_.LastWriteTime.ToString('s') }; foreach($f in 'probe-arm64.txt','probe-x64.txt'){ if(Test-Path $d\$f){ Get-Content $d\$f | Select-String -Pattern 'device |summary|clear_readback|compute |present1|video_device|removed' | ForEach-Object { 'K7 '+$f+' '+$_.Line } } }; foreach($f in 'cb26-stat.txt','edge-stat.txt'){ if(Test-Path $d\$f){ Get-Content $d\$f | Select-Object -First 20 | ForEach-Object { 'K7 '+$f+' '+$_ } } }"
echo GPUCOV K7 DONE
exit /b 0
