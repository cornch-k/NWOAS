#!/bin/bash
# NWOAS S256: build + test-sign the ACPI\NWAU0001 speaker driver end to end.
#   1. host unit tests of the hardware independent core (tests/run-tests.sh)
#   2. rsync the tree to /Volumes/media/nwoas/audio-build/src (VM: \\Mac\media\nwoas\audio-build\src)
#   3. prlctl exec "Windows 11" powershell build/build-audio.ps1 (SYSTEM, VS 2022 + WDK 26100,
#      Release|ARM64, WDK test signing with the GPU package certificate 13338B38...)
#   4. copy /Volumes/media/nwoas/audio-build/out/<stamp> back to ./out/<stamp> (delivery manifests point here)
# Usage: bash build-audio.sh [stamp-suffix]
# Build success is not working audio: nothing here touches the Mac mini.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
SHARE=/Volumes/media/nwoas/audio-build
VM="Windows 11"
STAMP=$(date +%Y%m%d-%H%M)${1:+-$1}
LOGDIR=$SHARE/logs
say() { echo "$(date +%T) $*"; }

[ -d /Volumes/media/nwoas ] || { echo "STOP /Volumes/media/nwoas is not mounted"; exit 1; }
mkdir -p "$SHARE/src" "$SHARE/scripts" "$SHARE/out" "$LOGDIR"

say "unit tests"
bash "$HERE/tests/run-tests.sh" > "$LOGDIR/unit-$STAMP.txt" 2>&1 || { echo "STOP unit tests"; tail -20 "$LOGDIR/unit-$STAMP.txt"; exit 1; }
tail -1 "$LOGDIR/unit-$STAMP.txt"

say "sync sources"
rsync -a --delete --exclude out/ --exclude build/ --exclude tests/ --exclude '.DS_Store' "$HERE/" "$SHARE/src/" || { echo "STOP rsync"; exit 1; }
cp "$HERE/build/build-audio.ps1" "$SHARE/scripts/build-audio.ps1" || { echo "STOP copy ps1"; exit 1; }

st=$(prlctl list -a 2>/dev/null | awk -v n="$VM" '{ s=$2; $1=$2=$3=""; sub(/^ +/,""); if ($0==n) print s }')
say "VM state: ${st:-unknown}"
case "$st" in
  suspended|paused) prlctl resume "$VM" >/dev/null || { echo "STOP resume"; exit 1; }; sleep 5 ;;
  stopped) prlctl start "$VM" >/dev/null || { echo "STOP start"; exit 1; }; sleep 60 ;;
esac

say "VM build stamp=$STAMP"
prlctl exec "$VM" powershell -NoProfile -ExecutionPolicy Bypass -File '\\Mac\media\nwoas\audio-build\scripts\build-audio.ps1' -Stamp "$STAMP" \
  > "$LOGDIR/vm-$STAMP.txt" 2>&1
rc=$?
head -c 20000 "$LOGDIR/vm-$STAMP.txt" | tr -d '\r' | tail -60
[ $rc = 0 ] || { echo "STOP VM build rc=$rc (full log $LOGDIR/vm-$STAMP.txt, msbuild log $SHARE/out/$STAMP/logs/build.log)"; exit 2; }

mkdir -p "$HERE/out"
rsync -a "$SHARE/out/$STAMP/" "$HERE/out/$STAMP/" || { echo "STOP copy back"; exit 1; }
say "package: $HERE/out/$STAMP/package"
( cd "$HERE/out/$STAMP/package" && shasum -a 256 * )
echo "BUILD DONE stamp=$STAMP"
