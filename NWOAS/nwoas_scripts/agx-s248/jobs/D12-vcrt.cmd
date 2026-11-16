@echo off
echo S248 D12 VC RUNTIME
for %%f in (MSVCP140.dll VCRUNTIME140.dll) do (if exist C:\Windows\System32\%%f (echo FOUND %%f) else (echo MISSING %%f))
reg query "HKLM\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\ARM64" 2>nul
exit /b 0
