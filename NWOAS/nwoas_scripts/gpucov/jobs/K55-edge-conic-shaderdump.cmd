@echo off
echo GPUCOV K55 EDGE CONIC SHADER DUMP (no driver change): Edge with --enable-gpu-rasterization --disable-gpu-sandbox on a page that is one conic gradient; the UMD writes every D3D11 shader it receives to C:\NWOAS\gpucov\dxbc-conic; zip, base64 (decode: bash gpucov/fetch-b64.sh K55 JOBLOG DIR)
set D=C:\NWOAS\gpucov
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
if exist %D%\dxbc-conic (echo K55 dxbc-conic already exists - kept, new shaders are added next to the old ones) else (mkdir %D%\dxbc-conic)
icacls %D%\dxbc-conic /grant *S-1-5-32-545:(OI)(CI)M /T >nul
> %D%\conic1.html echo ^<!doctype html^>^<body style="margin:0;height:100vh;background:conic-gradient(#e63946,#f4a261,#e9c46a,#2a9d8f,#264653,#e63946)"^>^</body^>
del /q %D%\edge-umd.log 2>nul
> %D%\k-edge.cmd echo @echo off
>> %D%\k-edge.cmd echo set APPLE_AGX_D3D_SHADER_DUMP=1
>> %D%\k-edge.cmd echo set APPLE_AGX_D3D_SHADER_DUMP_DIR=%D%\dxbc-conic
>> %D%\k-edge.cmd echo set APPLE_AGX_UMD_TRACE_FILE=%D%\edge-umd.log
>> %D%\k-edge.cmd echo set APPLE_AGX_UMD_REFUSALS_ONLY=1
>> %D%\k-edge.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File %D%\edge4.ps1 -Watch 25 -Url file:///C:/NWOAS/gpucov/conic1.html -Extra:"--enable-gpu-rasterization --disable-gpu-sandbox"
del /q %D%\edge2-done.txt 2>nul
powershell -NoProfile -Command "$t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'K55 explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds; Start-Sleep 4"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K55 -Cmd %D%\k-edge.cmd -Done %D%\edge2-done.txt -Timeout 240
powershell -NoProfile -Command "$d='C:\NWOAS\gpucov'; Get-Content ($d+'\edge2-stat.txt') -ErrorAction SilentlyContinue | Where-Object { $_ -match '^EDGE2' } | ForEach-Object { 'K55 '+$_.Substring(0,[Math]::Min(300,$_.Length)) }; $f=@(Get-ChildItem ($d+'\dxbc-conic') -File -ErrorAction SilentlyContinue); 'K55 FILES '+$f.Count+' bytes='+(($f | Measure-Object Length -Sum).Sum); $f | Group-Object { $_.Name.Split('-')[0] } | ForEach-Object { 'K55 STAGE '+$_.Name+' x'+$_.Count }; if(-not $f.Count){ 'K55 NO SHADERS'; exit }; $z=$d+'\dxbc-conic.zip'; Remove-Item $z -ErrorAction SilentlyContinue; Compress-Archive -Path ($d+'\dxbc-conic\*') -DestinationPath $z -Force; $i=Get-Item $z; 'K55 ZIP bytes='+$i.Length+' sha256='+(Get-FileHash $z -Algorithm SHA256).Hash.ToLower(); 'K55 BEGIN-BASE64'; [Convert]::ToBase64String([IO.File]::ReadAllBytes($z),'InsertLineBreaks'); 'K55 END-BASE64'"
echo GPUCOV K55 DONE
exit /b 0
