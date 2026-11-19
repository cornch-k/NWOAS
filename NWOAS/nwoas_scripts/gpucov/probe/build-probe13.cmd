@echo off
rem probe13 (after 2774 K43c: probe12 + memladder2 pre line, settle=, per-copy removal, more receipts). Builds nwgpuprobe for ARM64 (native D3D path) and x64 (Prism emulation, to show what x64 apps get).
rem Run inside the Parallels "Windows 11" VM: cmd /c \\Mac\media\nwoas\gpucov-build\build-probe13.cmd
setlocal
set S=\\Mac\media\nwoas\gpucov-build
set W=C:\agx\gpucov
if not exist %W% mkdir %W%
copy /y %S%\nwgpuprobe.cpp %W%\ >nul || exit /b 1
copy /y %S%\nwgpuprobe_tex.inc %W%\ >nul || exit /b 1
copy /y %S%\nwgpuprobe_ui.inc %W%\ >nul || exit /b 1
copy /y %S%\nwgpuprobe_comp.inc %W%\ >nul || exit /b 1
copy /y %S%\nwgpuprobe_interop.inc %W%\ >nul || exit /b 1
copy /y %S%\nwgpuprobe_d3d11ui.inc %W%\ >nul || exit /b 1
copy /y %S%\nwgpuprobe_chromium.inc %W%\ >nul || exit /b 1
copy /y %S%\nwgpuprobe_x64umd.inc %W%\ >nul || exit /b 1
copy /y %S%\nwgpuprobe_mem.inc %W%\ >nul || exit /b 1
copy /y %S%\nwgpuprobe_mem2.inc %W%\ >nul || exit /b 1
cd /d %W%
call C:\VS2022Community\VC\Auxiliary\Build\vcvarsarm64.bat >nul || exit /b 2
cl /nologo /EHsc /O2 /W3 /MT /Fe:nwgpuprobe13-arm64.exe nwgpuprobe.cpp /link /SUBSYSTEM:CONSOLE > build-arm64.log 2>&1
set RC1=%ERRORLEVEL%
type build-arm64.log
endlocal & set RC1=%RC1%
setlocal
set W=C:\agx\gpucov
cd /d %W%
call C:\VS2022Community\VC\Auxiliary\Build\vcvarsarm64_amd64.bat >nul || exit /b 3
cl /nologo /EHsc /O2 /W3 /MT /Fe:nwgpuprobe13-x64.exe nwgpuprobe.cpp /link /SUBSYSTEM:CONSOLE > build-x64.log 2>&1
set RC2=%ERRORLEVEL%
type build-x64.log
copy /y nwgpuprobe13-arm64.exe \\Mac\media\nwoas\gpucov-build\ >nul
copy /y nwgpuprobe13-x64.exe \\Mac\media\nwoas\gpucov-build\ >nul
del /q *.obj 2>nul
echo BUILD_PROBE arm64=%RC1% x64=%RC2%
endlocal
