@echo off
echo S248 D17b UMD TRACE OFF: remove the D17 machine-wide variables (new processes; DWM keeps its copy until the next restart); the log is kept, renamed
powershell -NoProfile -Command "foreach($n in 'APPLE_AGX_UMD_TRACE_FILE','APPLE_AGX_UMD_REFUSALS_ONLY'){ 'BEFORE '+$n+'='+[Environment]::GetEnvironmentVariable($n,'Machine'); [Environment]::SetEnvironmentVariable($n,$null,'Machine'); 'AFTER '+$n+'='+[Environment]::GetEnvironmentVariable($n,'Machine') }; $f='C:\NWOAS\umd-trace.log'; if(Test-Path $f){ $n='C:\NWOAS\umd-trace-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'.log'; Move-Item $f $n -ErrorAction SilentlyContinue; 'KEPT '+$n+' bytes='+(Get-Item $n -ErrorAction SilentlyContinue).Length }"
echo S248 D17b DONE
exit /b 0
