#!/bin/bash
# NWOAS S264D: reproducible build of the s264D hypervisor
#   m1n1-s264D = s264C (build-s264c.sh) + out-s264D/hv2-s264C-to-s264D.patch
#              (= sound stage B/S in C: src/nwoas_s264_audio.c, S264_STEP_AUDIO_B;
#                 NWOAS_WIFI=1 in C: src/nwoas_s264_wifi.c, S264_STEP_WIFI d = 1, smc_get_key_info)
# then the hash compare and the s264D offline test (host only).
# usage: build-s264d.sh <new work dir>       (installs nothing, touches no device)
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
WORK="${1:?usage: build-s264d.sh <new work dir>}"
[ -e "$WORK" ] && { echo "work dir exists: $WORK"; exit 2; }
D_SHA=2c3127d59814394a34c8f9ad0924fccbd446866bc5dfcc8f889481398a5ac17e
P_SHA=f439a05e7a41717ff0a0938aa6ffc919033014c33208f9dbbe8a68af1b830a2f
[ "$(shasum -a 256 "$HERE/out-s264D/hv2-s264C-to-s264D.patch" | awk '{print $1}')" = "$P_SHA" ] \
    || { echo 'hv2-s264C-to-s264D.patch differs from the recorded one'; exit 2; }
bash "$HERE/build-s264c.sh" "$WORK"
rsync -a "$WORK/src-s264C/" "$WORK/src-s264D/"
( cd "$WORK/src-s264D" && patch -p1 -s --no-backup-if-mismatch < "$HERE/out-s264D/hv2-s264C-to-s264D.patch" )
bash "$ROOT/port-j274/gpu/hv/tools-hv2/buildvar.sh" "$WORK/src-s264D" "$WORK/build-s264D" s264D NWOAS_AGX=1
BIN="$WORK/build-s264D/build/m1n1.bin"
echo "s264D: $(stat -f %z "$BIN") B sha256 $(shasum -a 256 "$BIN" | awk '{print $1}')"
[ "$(shasum -a 256 "$BIN" | awk '{print $1}')" = "$D_SHA" ] && echo "s264D reproduced: $D_SHA" \
    || { echo 's264D binary differs from the recorded one'; exit 1; }
S264_SRC="$WORK/src-s264D/src" "$ROOT/m1n1_windows/.venv-hv/bin/python3" -B "$HERE/s264d_offline_test.py" \
    --workdir "$WORK/offline-d" | grep -a "^| \|^S264D OFFLINE\|^S264C OFFLINE\|^\[s264D\]"
