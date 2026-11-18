@echo off
echo CB26 C2 RUN Cinebench 2026 CPU multi + single (minimum test duration 60 s each)
for /f "delims=" %%E in ('powershell -NoProfile -Command "(Get-ChildItem 'C:\NWOAS\cb26\app' -Recurse -Filter 'Cinebench.exe' | Select-Object -First 1).FullName"') do set "CB=%%E"
if not defined CB (echo CB26 C2 FAIL no Cinebench.exe & exit /b 1)
echo EXE %CB%
for %%T in (g_CinebenchCpuXTest g_CinebenchCpu1Test) do (
  echo === %%T
  powershell -NoProfile -Command "$t0=Get-Date; $p=Start-Process -FilePath $env:CB -ArgumentList '%%T=true','g_CinebenchMinimumTestDuration=60' -NoNewWindow -Wait -PassThru -RedirectStandardOutput C:\NWOAS\cb26\out-%%T.txt; 'EXIT '+$p.ExitCode+' sec='+[int]((Get-Date)-$t0).TotalSeconds; Get-Content C:\NWOAS\cb26\out-%%T.txt | Select-String -Pattern 'CB|Score|score|pts|Running|Error|error' | Select-Object -Last 15 | ForEach-Object { $_.Line }"
)
echo CB26 C2 DONE
exit /b 0
