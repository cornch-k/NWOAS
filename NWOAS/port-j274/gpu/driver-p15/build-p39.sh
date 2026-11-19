#!/bin/bash
# P39 (2775): P38 sources + recording only: KMD receipt Wom1PoolTimeline (last 64 VidMm page-table updates of allocations >= 16 MiB,
# their destruction, and every placement census that found pages outside segment 3). No placement or result changes.
# Same steps as build-p38.sh.
# Needs the one-time x64 Mesa NIR + dxil-spirv build: agx-build/scripts/05x-nir-x64.ps1 (done 11-19).
set -u
cd /Users/angigyeom/Desktop/DEV/NWOAS
rsync -a --delete --exclude out/ port-j274/gpu/driver-p15/ /Volumes/media/nwoas/gpu-driver/ || { echo "STOP rsync"; exit 1; }
STAMP=$(date +%Y%m%d-%H%M)-p39
RUN=${RUN:-j274g4p}
V=(prlctl exec "Windows 11"); SC='\\Mac\media\nwoas\agx-build\scripts'
n=0
run() { n=$((n+1)); local LOG=/Volumes/media/nwoas/build-logs/p39-step$n.log; echo "== $(date +%T) step$n $*"; "${V[@]}" "$@" > $LOG 2>&1; rc=$?; echo "rc=$rc"; tail -6 $LOG | cut -c1-300; [ $rc = 0 ] || { echo "STOP at $*"; tail -30 $LOG | cut -c1-300; exit 1; }; }
run powershell -NoProfile -ExecutionPolicy Bypass -File "$SC\\03-sources.ps1"
run cmd /c "$SC\\07-manifest.cmd"
run powershell -NoProfile -ExecutionPolicy Bypass -File "$SC\\08b-runtime.ps1" -Run $RUN
run powershell -NoProfile -ExecutionPolicy Bypass -File "$SC\\08x-runtime-x64.ps1" -Run $RUN
run powershell -NoProfile -ExecutionPolicy Bypass -File "$SC\\09-driver.ps1" -Run $RUN -PackageBuild 2775 -Stamp $STAMP
run powershell -NoProfile -ExecutionPolicy Bypass -File "$SC\\11-x64-umd.ps1" -Run $RUN -PackageBuild 2775 -Stamp $STAMP
run powershell -NoProfile -ExecutionPolicy Bypass -File "$SC\\12-package-arm64x.ps1" -Run $RUN -Stamp $STAMP
echo "BUILD DONE stamp=$STAMP"; ls -la /Volumes/media/nwoas/out/ | tail -2
