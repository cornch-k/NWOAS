@echo off
echo GPUCOV K59 PIXEL CENTRE A/B (2780 P45): psmath (32 expressions x ps_4_0/ps_5_0 against CPU values computed with the pixel centre). Default = fix on: every case must PASS. APPLE_AGX_D3D_PIXEL_CENTER=0 for one process = the 2779 behaviour: 18 PASS, 46 FAIL expected.
set D=C:\NWOAS\gpucov
if not exist %D%\nwgpuprobe16-arm64.exe (echo K59 probe16 missing & echo GPUCOV K59 DONE & exit /b 0)
powershell -NoProfile -Command "Get-CimInstance Win32_VideoController | ForEach-Object { 'K59 VC '+$_.Name+' | '+$_.DriverVersion+' | '+$_.Status }"
reg query "HKLM\SOFTWARE\NWOAS\AgxD3d" /v D3d11PixelCenter 2>nul || echo K59 D3d11PixelCenter absent = on
set APPLE_AGX_D3D_PIXEL_CENTER=
%D%\nwgpuprobe16-arm64.exe psmath > %D%\k59-on-arm64.txt 2>&1
%D%\nwgpuprobe16-x64.exe psmath > %D%\k59-on-x64.txt 2>&1
set APPLE_AGX_D3D_PIXEL_CENTER=0
%D%\nwgpuprobe16-arm64.exe psmath > %D%\k59-off-arm64.txt 2>&1
set APPLE_AGX_D3D_PIXEL_CENTER=
powershell -NoProfile -Command "foreach($n in 'on-arm64','on-x64','off-arm64'){ $l=@(Get-Content ('C:\NWOAS\gpucov\k59-'+$n+'.txt') -ErrorAction SilentlyContinue); $l | Where-Object { $_ -match 'psmath (begin|end)' -or ($n -like 'on-*' -and $_ -match 'result=FAIL|psmath map') } | Select-Object -First 120 | ForEach-Object { 'K59 '+$n+' '+$_ }; 'K59 '+$n+' SUMMARY lines='+$l.Count+' pass='+@($l -match 'psmath case=.*result=PASS').Count+' fail='+@($l -match 'psmath case=.*result=FAIL').Count }"
echo GPUCOV K59 DONE
exit /b 0
