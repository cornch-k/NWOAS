@echo off
echo S259 P2 MODERATE: 1 kHz -10 dBFS 30 s, endpoint 0 dB (launcher NWOAS_AUDIO_PROT=moderate)
set "S=C:\NWOAS\drop\snd-j274-20261118-1412-bridge\prot-tone.ps1"
if not exist "%S%" (echo S259 P2-moderate FAIL no prot-tone.ps1 & exit /b 1)
powershell -NoProfile -ExecutionPolicy Bypass -File "%S%" -Dbfs -10 -Seconds 30 -EndpointDb 0 -Freq 1000
echo S259 P2-moderate DONE
exit /b 0
