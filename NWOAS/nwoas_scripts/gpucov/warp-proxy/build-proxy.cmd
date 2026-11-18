@echo off
rem Builds the ARM64 d3d11.dll proxy (gpucov K18). Run in the VM: cmd /c \\Mac\media\nwoas\gpucov-build\build-proxy.cmd
setlocal
set S=\\Mac\media\nwoas\gpucov-build
set W=C:\agx\gpucov-proxy
if not exist %W% mkdir %W%
copy /y %S%\d3d11proxy.c %W%\ >nul

cd /d %W%
call C:\VS2022Community\VC\Auxiliary\Build\vcvarsarm64.bat >nul
cl /nologo /O2 /W3 /MT /LD d3d11proxy.c /Fe:d3d11.dll /link kernel32.lib > build.log 2>&1
set RC=%ERRORLEVEL%
type build.log
copy /y d3d11.dll %S%\d3d11-warp-proxy-arm64.dll >nul
echo BUILD_PROXY rc=%RC%
endlocal
