#!/bin/bash
# NWOAS S264: reproducible build of the C guest launcher hypervisor
#   m1n1-s264A = g262Q (m1n1-gpu-hv2 g260O + hv2-g260O-to-g262P.patch + hv2-g262P-to-g262Q.patch)
#              + port-j274/gpu/hv/hv2-g262Q-to-s264.patch
# then the offline equivalence test (host only) and a hash compare with the recorded binary.
# usage: build-s264.sh <new work dir>      (refuses an existing dir; installs nothing, touches no device)
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
HV="$ROOT/port-j274/gpu/hv"
WORK="${1:?usage: build-s264.sh <new work dir>}"
[ -e "$WORK" ] && { echo "work dir exists: $WORK"; exit 2; }
EXPECT_SHA="${S264_EXPECT_SHA:-178a004f963d424f17a9fbac1662c1ffe1d1dd24045a464ffffccf266de46c11}"
EXPECT_SIZE="${S264_EXPECT_SIZE:-2408448}"
PATCH_SHA="${S264_PATCH_SHA:-220611a7d7a21138cba9810c87057c63367dffc6204c245cf943aedbb5e39ede}"
[ -z "$PATCH_SHA" ] || [ "$(shasum -a 256 "$HV/hv2-g262Q-to-s264.patch" | awk '{print $1}')" = "$PATCH_SHA" ] \
    || { echo 'hv2-g262Q-to-s264.patch differs from the recorded one'; exit 2; }
bash "$HV/tools-hv2/build-g262Q.sh" "$WORK/base" > "$WORK.g262Q.log" 2>&1 || { tail -5 "$WORK.g262Q.log"; exit 1; }
mv "$WORK.g262Q.log" "$WORK/base/build-g262Q.log"
rsync -a --exclude build "$WORK/base/src-g262Q/" "$WORK/src-s264/"
( cd "$WORK/src-s264" && patch -p1 -s --no-backup-if-mismatch < "$HV/hv2-g262Q-to-s264.patch" )
bash "$HV/tools-hv2/buildvar.sh" "$WORK/src-s264" "$WORK/build-s264" s264A NWOAS_AGX=1
BIN="$WORK/build-s264/build/m1n1.bin"
echo "s264A: $(stat -f %z "$BIN") B sha256 $(shasum -a 256 "$BIN" | awk '{print $1}')"
if [ -n "$EXPECT_SHA" ]; then
    [ "$(stat -f %z "$BIN")" = "$EXPECT_SIZE" ] && [ "$(shasum -a 256 "$BIN" | awk '{print $1}')" = "$EXPECT_SHA" ] \
        && echo "s264A reproduced: $EXPECT_SHA" || { echo 's264A binary differs from the recorded one'; exit 1; }
fi
S264_SRC="$WORK/src-s264/src" "$ROOT/m1n1_windows/.venv-hv/bin/python3" -B "$HERE/s264_offline_test.py" --workdir "$WORK/offline" | tail -12
