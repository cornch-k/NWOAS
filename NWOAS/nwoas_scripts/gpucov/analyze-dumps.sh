#!/bin/bash
# Analyse crash dumps (e.g. Edge GPU-process Crashpad .dmp from K24) in the Parallels VM with cdb:
# exception record, faulting stack, and our UMD module. Uses Microsoft's public symbol server and the
# AGX UMD PDB of the installed driver build.
#   bash gpucov/analyze-dumps.sh <folder with .dmp files> <driver out dir, e.g. port-j274/gpu/driver-p15/out/j274-full-20261118-2014-p26>
set -eu
IN=$1; DRV=$2
SH=/Volumes/media/nwoas/gpucov-dumps
rm -rf "$SH"; mkdir -p "$SH/sym"
cp "$IN"/*.dmp "$SH/"
cp "$DRV"/symbols/*.pdb "$SH/sym/"
cat > "$SH/run.cmd" <<'CMD'
@echo off
set C="C:\Program Files (x86)\Windows Kits\10\Debuggers\arm64\cdb.exe"
if not exist C:\symcache mkdir C:\symcache
if not exist C:\agx\umdsym mkdir C:\agx\umdsym
copy /y \\Mac\media\nwoas\gpucov-dumps\sym\*.pdb C:\agx\umdsym\ >nul
for %%f in (\\Mac\media\nwoas\gpucov-dumps\*.dmp) do (
  echo ===== %%~nxf
  %C% -z "%%f" -y "C:\agx\umdsym;srv*C:\symcache*https://msdl.microsoft.com/download/symbols" -c ".lastevent; .ecxr; kn 40; lmvm AppleAgxRenderAdmissionUmd; lm o; q" 2>&1 | findstr /v /c:"Symbol search path" /c:"Executable search path"
)
CMD
prlctl exec "Windows 11" cmd /c '\\Mac\media\nwoas\gpucov-dumps\run.cmd' 2>&1 | iconv -f CP949 -t UTF-8 > "$IN/analysis.txt" || true
rm -rf "$SH"
echo "analysis: $IN/analysis.txt ($(wc -l < "$IN/analysis.txt") lines)"
grep -E '^=====|ExceptionCode|Last event|^ ?[0-9a-f]{2} [0-9a-f]+ [0-9a-f]+ ' "$IN/analysis.txt" | head -80
