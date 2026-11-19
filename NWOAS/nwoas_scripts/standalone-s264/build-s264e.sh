#!/bin/bash
# NWOAS S264E: reproducible build of the s264E hypervisor
#   m1n1-s264E = s264D (build-s264d.sh) + out-s264E/hv2-s264D-to-s264E.patch
#              (= bounded RTKit/SMC/AFK waits; WifiBtS261 (bt-s261) in src/nwoas_s264_wifi.c: BT page,
#                 fn1 BAR emulation + fence; S261 flags; sound protection default moderate)
# then the hash compare and the s264E offline test (host only).
# usage: build-s264e.sh <new work dir>       (installs nothing, touches no device)
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
WORK="${1:?usage: build-s264e.sh <new work dir>}"
[ -e "$WORK" ] && { echo "work dir exists: $WORK"; exit 2; }
E_SHA=9254d61922779ba997e8be1575868b0e92d8ec6ee7db3419fc39e0dd58949615
P_SHA=bd9c54224189b8070f90cfe2d44c3b28d78533671931537e1776fb1488b0f4b2
[ "$(shasum -a 256 "$HERE/out-s264E/hv2-s264D-to-s264E.patch" | awk '{print $1}')" = "$P_SHA" ] \
    || { echo 'hv2-s264D-to-s264E.patch differs from the recorded one'; exit 2; }
bash "$HERE/build-s264d.sh" "$WORK"
rsync -a "$WORK/src-s264D/" "$WORK/src-s264E/"
( cd "$WORK/src-s264E" && patch -p1 -s --no-backup-if-mismatch < "$HERE/out-s264E/hv2-s264D-to-s264E.patch" )
bash "$ROOT/port-j274/gpu/hv/tools-hv2/buildvar.sh" "$WORK/src-s264E" "$WORK/build-s264E" s264E NWOAS_AGX=1
BIN="$WORK/build-s264E/build/m1n1.bin"
echo "s264E: $(stat -f %z "$BIN") B sha256 $(shasum -a 256 "$BIN" | awk '{print $1}')"
[ "$(shasum -a 256 "$BIN" | awk '{print $1}')" = "$E_SHA" ] && echo "s264E reproduced: $E_SHA" \
    || { echo 's264E binary differs from the recorded one'; exit 1; }
S264_SRC="$WORK/src-s264E/src" "$ROOT/m1n1_windows/.venv-hv/bin/python3" -B "$HERE/s264e_offline_test.py" \
    --workdir "$WORK/offline-e" | grep -a "^| \|^S264E OFFLINE\|^S264C OFFLINE\|^\[s264E\]"
