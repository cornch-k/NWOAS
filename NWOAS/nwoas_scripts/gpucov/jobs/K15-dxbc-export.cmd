@echo off
echo GPUCOV K15 DXBC EXPORT: zip C:\NWOAS\gpucov\dxbc and print it as base64 between markers (decode on the Mac: gpucov/fetch-dxbc.sh)
set D=C:\NWOAS\gpucov
if not exist %D%\dxbc (echo K15 NO DXBC FOLDER & exit /b 0)
del /q %D%\dxbc.zip %D%\dxbc.b64 2>nul
powershell -NoProfile -Command "Compress-Archive -Path 'C:\NWOAS\gpucov\dxbc\*' -DestinationPath 'C:\NWOAS\gpucov\dxbc.zip' -Force; $z=Get-Item 'C:\NWOAS\gpucov\dxbc.zip'; $h=(Get-FileHash $z.FullName -Algorithm SHA256).Hash.ToLower(); 'K15 ZIP bytes='+$z.Length+' sha256='+$h; 'K15 BEGIN-BASE64'; [Convert]::ToBase64String([IO.File]::ReadAllBytes($z.FullName),'InsertLineBreaks'); 'K15 END-BASE64'"
echo GPUCOV K15 DONE
exit /b 0
