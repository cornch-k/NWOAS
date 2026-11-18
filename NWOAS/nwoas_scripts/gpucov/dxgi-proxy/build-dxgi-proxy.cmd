@echo off
rem Builds the ARM64 dxgi.dll proxy (gpucov K21). Run in the VM: cmd /c \\Mac\media\nwoas\gpucov-build\build-dxgi-proxy.cmd
setlocal
set S=\\Mac\media\nwoas\gpucov-build
set W=C:\agx\gpucov-dxgi
if not exist %W% mkdir %W%
copy /y %S%\dxgiproxy.c %W%\ >nul
cd /d %W%
call C:\VS2022Community\VC\Auxiliary\Build\vcvarsarm64.bat >nul
cl /nologo /O2 /W3 /MT /LD dxgiproxy.c /Fe:dxgi.dll /link kernel32.lib user32.lib dxguid.lib > build.log 2>&1
set RC=%ERRORLEVEL%
type build.log
copy /y dxgi.dll %S%\dxgi-trace-proxy-arm64.dll >nul
echo BUILD_DXGI_PROXY rc=%RC%
endlocal
