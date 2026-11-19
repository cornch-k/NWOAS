#!/bin/bash
# NWOAS S264G: reproducible build of the s264G hypervisor
#   m1n1-s264G = s264F (build-s264f.sh) + out-s264G/hv2-s264F-to-s264G.patch (4-2 auto-start, Rust ESP read)
# (s264F line kept from the copy:)
# NWOAS S264F: reproducible build of the s264F hypervisor
#   m1n1-s264F = s264E (build-s264e.sh) + out-s264F/hv2-s264E-to-s264F.patch (power_module.py in C)
# (s264E line kept from the copy:)
#   m1n1-s264E = s264D (build-s264d.sh) + out-s264E/hv2-s264D-to-s264E.patch
#              (= bounded RTKit/SMC/AFK waits; WifiBtS261 (bt-s261) in src/nwoas_s264_wifi.c: BT page,
#                 fn1 BAR emulation + fence; S261 flags; sound protection default moderate)
# then the hash compare and the s264E offline test (host only).
# usage: build-s264g.sh <new work dir>       (installs nothing, touches no device)
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
WORK="${1:?usage: build-s264g.sh <new work dir>}"
[ -e "$WORK" ] && { echo "work dir exists: $WORK"; exit 2; }
G_SHA=d1fea3d73ad7d0d7da562802dbdec88527490dfe5f395d28dbd464a324c43ce6
P_SHA=d688e7ebf4fe904b5cd8dbb87c3acbc9c067ad784ea60bf8bd888f6c6655cfa9
[ "$(shasum -a 256 "$HERE/out-s264G/hv2-s264F-to-s264G.patch" | awk '{print $1}')" = "$P_SHA" ] \
    || { echo 'hv2-s264F-to-s264G.patch differs from the recorded one'; exit 2; }
bash "$HERE/build-s264f.sh" "$WORK"
rsync -a "$WORK/src-s264F/" "$WORK/src-s264G/"
( cd "$WORK/src-s264G" && patch -p1 -s --no-backup-if-mismatch < "$HERE/out-s264G/hv2-s264F-to-s264G.patch" )
bash "$ROOT/port-j274/gpu/hv/tools-hv2/buildvar.sh" "$WORK/src-s264G" "$WORK/build-s264G" s264G NWOAS_AGX=1
BIN="$WORK/build-s264G/build/m1n1.bin"
echo "s264G: $(stat -f %z "$BIN") B sha256 $(shasum -a 256 "$BIN" | awk '{print $1}')"
[ "$(shasum -a 256 "$BIN" | awk '{print $1}')" = "$G_SHA" ] && echo "s264G reproduced: $G_SHA" \
    || { echo 's264G binary differs from the recorded one'; exit 1; }
S264_SRC="$WORK/src-s264G/src" "$ROOT/m1n1_windows/.venv-hv/bin/python3" -B "$HERE/s264g_offline_test.py" \
    --workdir "$WORK/offline-g" | grep -a "^| \|^S264G OFFLINE\|^S264F OFFLINE\|^\[s264G\]"
