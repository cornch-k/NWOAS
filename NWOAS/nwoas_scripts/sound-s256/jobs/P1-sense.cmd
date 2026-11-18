@echo off
echo S259 P1 SENSE ONLY: 1 kHz -10 dBFS 20 s, endpoint 0 dB (gain must stay 0x00)
set "S=C:\NWOAS\drop\snd-j274-20261118-1412-bridge\prot-tone.ps1"
if not exist "%S%" (echo S259 P1-sense FAIL no prot-tone.ps1 & exit /b 1)
powershell -NoProfile -ExecutionPolicy Bypass -File "%S%" -Dbfs -10 -Seconds 20 -EndpointDb 0 -Freq 1000
echo S259 P1-sense DONE
exit /b 0
