#!/bin/bash
# NWOAS S264C: reproducible build of the s264C hypervisor
#   m1n1-s264C = s264B (build-s264.sh: g262Q + hv2-g262Q-to-s264.patch + hv2-s264A-to-s264B.patch)
#              + out-s264C/hv2-s264B-to-s264C.patch  (= port-j274/gpu/hv/hv2-g262Q-to-g262R.patch, the g262R
#                ANS 5-cell interrupt fix, + S264_STEP_NVME_QD + exception policy S264_POL_LOCAL/FATAL)
# then the hash compare and the s264C offline test (host only).
# usage: build-s264c.sh <new work dir>       (installs nothing, touches no device)
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
WORK="${1:?usage: build-s264c.sh <new work dir>}"
[ -e "$WORK" ] && { echo "work dir exists: $WORK"; exit 2; }
C_SHA=6e6182c0d7a36ef09174d2c6af96142f5d2534476cf2ca292db18f8b3ce0ae0d
P_SHA=ce1e1478e3bea775ba7a9ba930bbac71ec08006c6c515fcf0b44c5e9298c15d9
[ "$(shasum -a 256 "$HERE/out-s264C/hv2-s264B-to-s264C.patch" | awk '{print $1}')" = "$P_SHA" ] \
    || { echo 'hv2-s264B-to-s264C.patch differs from the recorded one'; exit 2; }
bash "$HERE/build-s264.sh" "$WORK"
rsync -a "$WORK/src-s264B/" "$WORK/src-s264C/"
( cd "$WORK/src-s264C" && patch -p1 -s --no-backup-if-mismatch < "$HERE/out-s264C/hv2-s264B-to-s264C.patch" )
bash "$ROOT/port-j274/gpu/hv/tools-hv2/buildvar.sh" "$WORK/src-s264C" "$WORK/build-s264C" s264C NWOAS_AGX=1
BIN="$WORK/build-s264C/build/m1n1.bin"
echo "s264C: $(stat -f %z "$BIN") B sha256 $(shasum -a 256 "$BIN" | awk '{print $1}')"
[ "$(shasum -a 256 "$BIN" | awk '{print $1}')" = "$C_SHA" ] && echo "s264C reproduced: $C_SHA" \
    || { echo 's264C binary differs from the recorded one'; exit 1; }
S264_SRC="$WORK/src-s264C/src" "$ROOT/m1n1_windows/.venv-hv/bin/python3" -B "$HERE/s264c_offline_test.py" \
    --workdir "$WORK/offline-c" | grep -a "^\[s264C\]\|^S264C OFFLINE\|^\[iboot\] [0-9P]\|^\[chainload\] [0-9P]"
