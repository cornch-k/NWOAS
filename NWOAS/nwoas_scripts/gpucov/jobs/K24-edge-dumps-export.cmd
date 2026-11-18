@echo off
echo GPUCOV K24 EDGE CRASH DUMP EXPORT: zip the fresh profile's Crashpad reports and print base64 (decode: bash gpucov/fetch-b64.sh K24 JOBLOG DIR)
set D=C:\NWOAS\gpucov
set R=%D%\edge-prof2\Crashpad\reports
if not exist %R% (echo K24 NO REPORTS FOLDER & exit /b 0)
del /q %D%\edge-dumps.zip 2>nul
powershell -NoProfile -Command "$r='C:\NWOAS\gpucov\edge-prof2\Crashpad\reports'; $f=@(Get-ChildItem $r -File); 'K24 FILES '+$f.Count; $f | ForEach-Object { 'K24 F '+$_.Name+' '+$_.Length+' '+$_.LastWriteTime.ToString('s') }; Compress-Archive -Path ($r+'\*') -DestinationPath 'C:\NWOAS\gpucov\edge-dumps.zip' -Force; $z=Get-Item 'C:\NWOAS\gpucov\edge-dumps.zip'; 'K24 ZIP bytes='+$z.Length+' sha256='+(Get-FileHash $z.FullName -Algorithm SHA256).Hash.ToLower(); 'K24 BEGIN-BASE64'; [Convert]::ToBase64String([IO.File]::ReadAllBytes($z.FullName),'InsertLineBreaks'); 'K24 END-BASE64'"
echo GPUCOV K24 DONE
exit /b 0
