#!/bin/bash
# P29 (2765): DDI rings, registry-armed UMD trace, DXGI entry receipts (+P28..P22). Same steps as build-p21.sh.
set -u
cd /Users/angigyeom/Desktop/DEV/NWOAS
rsync -a --delete --exclude out/ port-j274/gpu/driver-p15/ /Volumes/media/nwoas/gpu-driver/ || { echo "STOP rsync"; exit 1; }
STAMP=$(date +%Y%m%d-%H%M)-p29
V=(prlctl exec "Windows 11"); SC='\\Mac\media\nwoas\agx-build\scripts'
n=0
run() { n=$((n+1)); local LOG=/Volumes/media/nwoas/build-logs/p29-step$n.log; echo "== $(date +%T) step$n $*"; "${V[@]}" "$@" > $LOG 2>&1; rc=$?; echo "rc=$rc"; tail -4 $LOG | cut -c1-300; [ $rc = 0 ] || { echo "STOP at $*"; tail -30 $LOG | cut -c1-300; exit 1; }; }
run powershell -NoProfile -ExecutionPolicy Bypass -File "$SC\\03-sources.ps1"
run cmd /c "$SC\\07-manifest.cmd"
run powershell -NoProfile -ExecutionPolicy Bypass -File "$SC\\08b-runtime.ps1" -Run j274g4c
run powershell -NoProfile -ExecutionPolicy Bypass -File "$SC\\09-driver.ps1" -Run j274g4c -PackageBuild 2765 -Stamp $STAMP
run powershell -NoProfile -ExecutionPolicy Bypass -File "$SC\\10-collect.ps1" -Run j274g4c -Stamp $STAMP
echo "BUILD DONE stamp=$STAMP"; ls -la /Volumes/media/nwoas/out/ | tail -2
