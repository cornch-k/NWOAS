@echo off
echo S140: read-only WinSAT disk validation after CPU P-state initialization
winsat.exe disk -seq -read -n 0
echo S140 WINSAT READ COMPLETE rc=%errorlevel%
