@echo off
echo S248 R35b remove the 2773 backing UMDs from System32 (only when the installed driver is 2772)
powershell -NoProfile -Command "$v=(Get-CimInstance Win32_VideoController | Where-Object { $_.Name -like 'Apple AGX*' }).DriverVersion; 'VC '+$v; if($v -ne '30.0.2772.0'){ 'R35b SKIP: driver is not 2772'; exit 0 }; foreach($n in 'AppleAgxRenderAdmissionUmdArm64.dll','AppleAgxRenderAdmissionUmdX64.dll'){ $f='C:\Windows\System32\'+$n; if(Test-Path $f){ try { Remove-Item $f -Force -ErrorAction Stop; 'R35b REMOVED '+$n } catch { 'R35b IN USE '+$n+' (restart and run again)' } } else { 'R35b absent '+$n } }"
echo S248 R35b DONE
exit /b 0
