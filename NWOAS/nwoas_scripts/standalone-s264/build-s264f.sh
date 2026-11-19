#!/bin/bash
# NWOAS S264F: reproducible build of the s264F hypervisor
#   m1n1-s264F = s264E (build-s264e.sh) + out-s264F/hv2-s264E-to-s264F.patch (power_module.py in C)
# (s264E line kept from the copy:)
#   m1n1-s264E = s264D (build-s264d.sh) + out-s264E/hv2-s264D-to-s264E.patch
#              (= bounded RTKit/SMC/AFK waits; WifiBtS261 (bt-s261) in src/nwoas_s264_wifi.c: BT page,
#                 fn1 BAR emulation + fence; S261 flags; sound protection default moderate)
# then the hash compare and the s264E offline test (host only).
# usage: build-s264f.sh <new work dir>       (installs nothing, touches no device)
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
WORK="${1:?usage: build-s264f.sh <new work dir>}"
[ -e "$WORK" ] && { echo "work dir exists: $WORK"; exit 2; }
F_SHA=9db7de2ae0ef1a8afb781bc3246d8a33795465524015a166b9998717868fc8ea
P_SHA=1b2771bb0fff9a33d5ed1b61e77074fc290bf877b40e857fa6ddf25bc5a52f72
[ "$(shasum -a 256 "$HERE/out-s264F/hv2-s264E-to-s264F.patch" | awk '{print $1}')" = "$P_SHA" ] \
    || { echo 'hv2-s264E-to-s264F.patch differs from the recorded one'; exit 2; }
bash "$HERE/build-s264e.sh" "$WORK"
rsync -a "$WORK/src-s264E/" "$WORK/src-s264F/"
( cd "$WORK/src-s264F" && patch -p1 -s --no-backup-if-mismatch < "$HERE/out-s264F/hv2-s264E-to-s264F.patch" )
bash "$ROOT/port-j274/gpu/hv/tools-hv2/buildvar.sh" "$WORK/src-s264F" "$WORK/build-s264F" s264F NWOAS_AGX=1
BIN="$WORK/build-s264F/build/m1n1.bin"
echo "s264F: $(stat -f %z "$BIN") B sha256 $(shasum -a 256 "$BIN" | awk '{print $1}')"
[ "$(shasum -a 256 "$BIN" | awk '{print $1}')" = "$F_SHA" ] && echo "s264F reproduced: $F_SHA" \
    || { echo 's264F binary differs from the recorded one'; exit 1; }
S264_SRC="$WORK/src-s264F/src" "$ROOT/m1n1_windows/.venv-hv/bin/python3" -B "$HERE/s264f_offline_test.py" \
    --workdir "$WORK/offline-f" | grep -a "^| \|^S264F OFFLINE\|^S264E OFFLINE\|^\[s264F\]"
