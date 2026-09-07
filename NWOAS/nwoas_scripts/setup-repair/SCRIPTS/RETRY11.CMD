@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem Use only after leaving the current Setup instance. Never terminate Setup here.
call "%~dp0FIX11.CMD"
if errorlevel 1 exit /b 2
set "MEDIA=%~d0"
set "IMAGE="
if exist "%MEDIA%\sources\install.esd" set "IMAGE=%MEDIA%\sources\install.esd"
if exist "%MEDIA%\sources\install.wim" set "IMAGE=%MEDIA%\sources\install.wim"
if exist "%MEDIA%\sources\install.swm" set "IMAGE=%MEDIA%\sources\install.swm"
if not defined IMAGE exit /b 2
if not exist "%MEDIA%\SCRIPTS\interactive.xml" exit /b 2
if not exist "%SystemDrive%\sources\setup.exe" exit /b 2
cscript //nologo "%~dp0setup-idle.vbs"
if errorlevel 1 exit /b 4
echo Starting interactive Setup using %IMAGE%
start "NWOAS interactive Setup" /wait "%SystemDrive%\sources\setup.exe" /installfrom "%IMAGE%" /unattend:"%MEDIA%\SCRIPTS\interactive.xml"
set "SETUP_RC=%ERRORLEVEL%"
call "%~dp0COLLECT11.CMD"
echo Setup process exit code: %SETUP_RC%
exit /b %SETUP_RC%
