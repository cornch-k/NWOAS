#!/bin/bash
# NWOAS Bluetooth: build + test-sign the BCM4378 BthX transport driver package (nwbt4378.sys) end to end.
# Copy of port-j274/wifi/bcm4378-wificx/build-wifi.sh:
#   1. host unit tests of the hardware independent core (tests/run-tests.sh)
#   2. firmware set for the package from this Mac's macOS /usr/share/firmware/bluetooth (BCM4378 B1,
#      board AtlantisB = J274; MUR -> vendor m, USI -> vendor u, the names the driver asks for)
#   3. rsync the tree to /Volumes/media/nwoas/bt-build/src (VM: \\Mac\media\nwoas\bt-build\src)
#   4. prlctl exec "Windows 11" powershell build/build-bt.ps1 (SYSTEM, VS 2022 + WDK 26100,
#      Release|ARM64, signed with the GPU/audio/Wi-Fi test certificate 13338B38...)
#   5. copy /Volumes/media/nwoas/bt-build/out/<stamp> back to ./out/<stamp>
# Usage: bash build-bt.sh [stamp-suffix]
# Build success is not working Bluetooth: nothing here touches the Mac mini.
# The firmware files are Apple's: they only go into the private package, never into the repo.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
SHARE=/Volumes/media/nwoas/bt-build
VM="Windows 11"
STAMP=$(date +%Y%m%d-%H%M)${1:+-$1}
LOGDIR=$SHARE/logs
FWSRC=/usr/share/firmware/bluetooth
say() { echo "$(date +%T) $*"; }

[ -d /Volumes/media/nwoas ] || { echo "STOP /Volumes/media/nwoas is not mounted"; exit 1; }
mkdir -p "$SHARE/src" "$SHARE/out" "$SHARE/fw" "$LOGDIR"

say "unit tests"
bash "$HERE/tests/run-tests.sh" > "$LOGDIR/unit-$STAMP.txt" 2>&1 || { echo "STOP unit tests"; tail -20 "$LOGDIR/unit-$STAMP.txt"; exit 1; }
tail -1 "$LOGDIR/unit-$STAMP.txt"

say "firmware set"
rm -f "$SHARE/fw/"*
pick() {  # $1 glob, $2 destination name: exactly one match or stop
  set -- $1 "$2"
  [ $# -eq 2 ] && [ -f "$1" ] || { echo "STOP firmware match for $2: $*"; exit 1; }
  cp -L "$1" "$SHARE/fw/$2" || exit 1
  echo "fw $2 <- $(basename "$1")"
}
pick "$FWSRC/BCM4378B1_*_PCIE_macOS_AtlantisB_OS_MUR_*.bin" bt4378b1-atlantisb-m.bin
pick "$FWSRC/BCM4378B1_*_PCIE_macOS_AtlantisB_OS_USI_*.bin" bt4378b1-atlantisb-u.bin
pick "$FWSRC/BCM4378B1_*_PCIE_macOS_AtlantisB_OS_MUR_*.ptb" bt4378b1-atlantisb-m.ptb
pick "$FWSRC/BCM4378B1_*_PCIE_macOS_AtlantisB_OS_USI_*.ptb" bt4378b1-atlantisb-u.ptb
( cd "$SHARE/fw" && shasum -a 256 * | sed 's/^/fw /' )

say "sync sources"
rsync -a --delete --exclude out/ --exclude build/ --exclude tests/ --exclude tools/ --exclude '.DS_Store' \
  "$HERE/" "$SHARE/src/" || { echo "STOP rsync"; exit 1; }
mkdir -p "$SHARE/scripts"
cp "$HERE/build/build-bt.ps1" "$SHARE/scripts/build-bt.ps1" || { echo "STOP copy ps1"; exit 1; }

st=$(prlctl list -a 2>/dev/null | awk -v n="$VM" '{ s=$2; $1=$2=$3=""; sub(/^ +/,""); if ($0==n) print s }')
say "VM state: ${st:-unknown}"
case "$st" in
  suspended|paused) prlctl resume "$VM" >/dev/null || { echo "STOP resume"; exit 1; }; sleep 5 ;;
  stopped) prlctl start "$VM" >/dev/null || { echo "STOP start"; exit 1; }; sleep 60 ;;
esac

say "VM build stamp=$STAMP"
prlctl exec "$VM" powershell -NoProfile -ExecutionPolicy Bypass -File '\\Mac\media\nwoas\bt-build\scripts\build-bt.ps1' -Stamp "$STAMP" \
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
