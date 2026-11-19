#!/bin/bash
# P42: P40 (2776) sources + the x86 (WOW64) D3D UMD (AppleAgxRenderAdmissionUmd32.dll, SysWOW64,
# UserModeDriverNameWow) packaged with AppleAgxRenderAdmission.arm64xwow.inf. Same steps as build-p40.sh plus
# 05y (x86 Mesa NIR + dxil-spirv x86, once per builder; skips what exists), 08y (x86 runtime closure),
# 11y (x86 UMD, vcxproj Release|Win32) and 12y (package with the x86 UMD) instead of 12.
# PACKAGE_BUILD (default 2777) sets the version. Uses the Parallels "Windows 11" VM (ask before running).
set -u
cd /Users/angigyeom/Desktop/DEV/NWOAS
PB=${PACKAGE_BUILD:-2777}
rsync -a --delete --exclude out/ port-j274/gpu/driver-p15/ /Volumes/media/nwoas/gpu-driver/ || { echo "STOP rsync"; exit 1; }
cp port-j274/gpu/driver-p15/agx-build-p42/*.ps1 /Volumes/media/nwoas/agx-build/scripts/ || { echo "STOP copy scripts"; exit 1; }
STAMP=$(date +%Y%m%d-%H%M)-p42
RUN=${RUN:-j274g4r}
V=(prlctl exec "Windows 11"); SC='\\Mac\media\nwoas\agx-build\scripts'
n=0
run() { n=$((n+1)); local LOG=/Volumes/media/nwoas/build-logs/p42-step$n.log; echo "== $(date +%T) step$n $*"; "${V[@]}" "$@" > $LOG 2>&1; rc=$?; echo "rc=$rc"; tail -8 $LOG | cut -c1-300; [ $rc = 0 ] || { echo "STOP at $*"; tail -30 $LOG | cut -c1-300; exit 1; }; }
run powershell -NoProfile -ExecutionPolicy Bypass -File "$SC\\03-sources.ps1"
run cmd /c "$SC\\07-manifest.cmd"
run powershell -NoProfile -ExecutionPolicy Bypass -File "$SC\\05y-nir-x86.ps1"
run powershell -NoProfile -ExecutionPolicy Bypass -File "$SC\\08b-runtime.ps1" -Run $RUN
run powershell -NoProfile -ExecutionPolicy Bypass -File "$SC\\08x-runtime-x64.ps1" -Run $RUN
run powershell -NoProfile -ExecutionPolicy Bypass -File "$SC\\08y-runtime-x86.ps1" -Run $RUN
run powershell -NoProfile -ExecutionPolicy Bypass -File "$SC\\09-driver.ps1" -Run $RUN -PackageBuild $PB -Stamp $STAMP
run powershell -NoProfile -ExecutionPolicy Bypass -File "$SC\\11-x64-umd.ps1" -Run $RUN -PackageBuild $PB -Stamp $STAMP
run powershell -NoProfile -ExecutionPolicy Bypass -File "$SC\\11y-x86-umd.ps1" -Run $RUN -PackageBuild $PB -Stamp $STAMP
run powershell -NoProfile -ExecutionPolicy Bypass -File "$SC\\12y-package-arm64xwow.ps1" -Run $RUN -Stamp $STAMP
echo "BUILD DONE stamp=$STAMP"; ls -la /Volumes/media/nwoas/out/ | tail -2
