@echo off
echo CB26 C1 DOWNLOAD Cinebench2026_win_arm64.zip (Maxon official, 783373346 bytes) AND EXTRACT
set "D=C:\NWOAS\cb26"
if not exist "%D%" mkdir "%D%"
powershell -NoProfile -Command "$ProgressPreference='SilentlyContinue'; $u='https://mx-app-blob-prod.maxon.net/mx-package-production/website/windows/maxon/cinebench/Cinebench2026_win_arm64.zip'; $z='C:\NWOAS\cb26\cb26.zip'; $t=Measure-Command { Invoke-WebRequest -UseBasicParsing -Uri $u -OutFile $z }; $len=(Get-Item $z).Length; 'DL bytes='+$len+' sec='+[math]::Round($t.TotalSeconds,1)+' Mbps='+[math]::Round($len*8/$t.TotalSeconds/1e6,1); if($len -ne 783373346){'DL SIZE MISMATCH'; exit 1}; 'SHA256 '+(Get-FileHash $z -Algorithm SHA256).Hash; Expand-Archive -Path $z -DestinationPath 'C:\NWOAS\cb26\app' -Force; Remove-Item $z; Get-ChildItem 'C:\NWOAS\cb26\app' -Recurse -Filter 'Cinebench*.exe' | ForEach-Object { 'EXE '+$_.FullName+' '+$_.Length }"
echo CB26 C1 DONE
exit /b 0
