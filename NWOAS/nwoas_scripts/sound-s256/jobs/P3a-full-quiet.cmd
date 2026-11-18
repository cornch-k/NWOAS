@echo off
echo S259 P3a FULL: 1 kHz -20 dBFS 20 s, endpoint 0 dB (launcher NWOAS_AUDIO_PROT=full)
set "S=C:\NWOAS\drop\snd-j274-20261118-1412-bridge\prot-tone.ps1"
if not exist "%S%" (echo S259 P3a-full-quiet FAIL no prot-tone.ps1 & exit /b 1)
powershell -NoProfile -ExecutionPolicy Bypass -File "%S%" -Dbfs -20 -Seconds 20 -EndpointDb 0 -Freq 1000
echo S259 P3a-full-quiet DONE
exit /b 0
