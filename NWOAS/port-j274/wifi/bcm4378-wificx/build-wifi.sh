#!/bin/bash
# NWOAS Wi-Fi: build + test-sign the BCM4378 WiFiCx driver package (nwoa4378.sys) end to end.
#   1. host unit tests of the hardware independent core (tests/run-tests.sh)
#   2. firmware set for the package from this Mac's macOS (/usr/share/firmware/wifi/C-4378__s-B1,
#      symlinks resolved) + nwoas-bcm4378-board.bin (ADT cal + MAC) from the offline ADT dump
#   3. rsync the tree to /Volumes/media/nwoas/wifi-build/src (VM: \\Mac\media\nwoas\wifi-build\src)
#   4. prlctl exec "Windows 11" powershell build/build-wifi.ps1 (SYSTEM, VS 2022 + WDK 26100,
#      Release|ARM64, signed with the GPU/audio test certificate 13338B38...)
#   5. copy /Volumes/media/nwoas/wifi-build/out/<stamp> back to ./out/<stamp>
# Usage: bash build-wifi.sh [stamp-suffix]
# Build success is not working Wi-Fi: nothing here touches the Mac mini.
# The firmware files are Apple's: they only go into the private package, never into the repo.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
SHARE=/Volumes/media/nwoas/wifi-build
VM="Windows 11"
STAMP=$(date +%Y%m%d-%H%M)${1:+-$1}
LOGDIR=$SHARE/logs
FWSRC=/usr/share/firmware/wifi/C-4378__s-B1
ADT=$ROOT/experiments/tahoe-afk-20260906/current.adt
PY=$ROOT/m1n1_windows/.venv-hv/bin/python3
say() { echo "$(date +%T) $*"; }

[ -d /Volumes/media/nwoas ] || { echo "STOP /Volumes/media/nwoas is not mounted"; exit 1; }
mkdir -p "$SHARE/src" "$SHARE/out" "$SHARE/fw" "$LOGDIR"

say "unit tests"
bash "$HERE/tests/run-tests.sh" > "$LOGDIR/unit-$STAMP.txt" 2>&1 || { echo "STOP unit tests"; tail -20 "$LOGDIR/unit-$STAMP.txt"; exit 1; }
tail -1 "$LOGDIR/unit-$STAMP.txt"

say "firmware set"
rm -f "$SHARE/fw/"*
cp -L "$FWSRC/atlantisb-X3.trx" "$SHARE/fw/atlantisb-X3.trx" &&
cp -L "$FWSRC/atlantisb-X3.clmb" "$SHARE/fw/atlantisb-X3.clmb" &&
cp -L "$FWSRC/atlantisb-X3.txcb" "$SHARE/fw/atlantisb-X3.txcb" || { echo "STOP firmware copy"; exit 1; }
for V in m u; do
  set -- "$FWSRC"/P-atlantisb_M-RASP_V-${V}__m-*.txt
  [ -f "$1" ] || { echo "STOP no NVRAM for vendor $V"; exit 1; }
  for f in "$@"; do cmp -s "$1" "$f" || { echo "STOP NVRAM versions differ for vendor $V ($f)"; exit 1; }; done
  cp -L "$1" "$SHARE/fw/P-atlantisb_M-RASP_V-$V.txt" || exit 1
done
"$PY" -I "$HERE/tools/make_board_blob.py" "$ADT" "$SHARE/fw/nwoas-bcm4378-board.bin" || { echo "STOP board file"; exit 1; }
( cd "$SHARE/fw" && shasum -a 256 * | sed 's/^/fw /' )

say "sync sources"
rsync -a --delete --exclude out/ --exclude build/ --exclude tests/ --exclude tools/ --exclude '.DS_Store' \
  "$HERE/" "$SHARE/src/" || { echo "STOP rsync"; exit 1; }
mkdir -p "$SHARE/scripts"
cp "$HERE/build/build-wifi.ps1" "$SHARE/scripts/build-wifi.ps1" || { echo "STOP copy ps1"; exit 1; }

st=$(prlctl list -a 2>/dev/null | awk -v n="$VM" '{ s=$2; $1=$2=$3=""; sub(/^ +/,""); if ($0==n) print s }')
say "VM state: ${st:-unknown}"
case "$st" in
  suspended|paused) prlctl resume "$VM" >/dev/null || { echo "STOP resume"; exit 1; }; sleep 5 ;;
  stopped) prlctl start "$VM" >/dev/null || { echo "STOP start"; exit 1; }; sleep 60 ;;
esac

say "VM build stamp=$STAMP"
prlctl exec "$VM" powershell -NoProfile -ExecutionPolicy Bypass -File '\\Mac\media\nwoas\wifi-build\scripts\build-wifi.ps1' -Stamp "$STAMP" \
  > "$LOGDIR/vm-$STAMP.txt" 2>&1
rc=$?
head -c 30000 "$LOGDIR/vm-$STAMP.txt" | tr -d '\r' | tail -80
rm -f "$SHARE/fw/"*
[ $rc = 0 ] || { echo "STOP VM build rc=$rc (full log $LOGDIR/vm-$STAMP.txt, msbuild log $SHARE/out/$STAMP/logs/build.log)"; exit 2; }

mkdir -p "$HERE/out"
rsync -a "$SHARE/out/$STAMP/" "$HERE/out/$STAMP/" || { echo "STOP copy back"; exit 1; }
say "package: $HERE/out/$STAMP/package"
( cd "$HERE/out/$STAMP/package" && shasum -a 256 * )
echo "BUILD DONE stamp=$STAMP"
