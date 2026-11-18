@echo off
echo S259 P3b FULL: 1 kHz -10 dBFS 60 s, endpoint 0 dB (model must limit)
set "S=C:\NWOAS\drop\snd-j274-20261118-1412-bridge\prot-tone.ps1"
if not exist "%S%" (echo S259 P3b-full-tone FAIL no prot-tone.ps1 & exit /b 1)
powershell -NoProfile -ExecutionPolicy Bypass -File "%S%" -Dbfs -10 -Seconds 60 -EndpointDb 0 -Freq 1000
echo S259 P3b-full-tone DONE
exit /b 0
