#!/bin/bash
# NWOAS PMU cycle-counter fix: reproducible builds (host only; installs nothing, touches no device).
#   bash build-pmu.sh g262T <new work dir>   m1n1-g262T = g262R (tools-hv2/build-g262R.sh) + hv2-g262R-to-g262T.patch
#   bash build-pmu.sh s264H <new work dir>   m1n1-s264H = s264G (standalone-s264/build-s264g.sh) + hv2-s264G-to-s264H.patch
# BASE_WORK=<existing work dir of that base script> skips rebuilding the base (its binary sha is still checked).
# Also runs hosttest/test_pmu_filter.c against the patched source.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
V="${1:?usage: build-pmu.sh g262T|s264H <new work dir>}"; WORK="${2:?usage}"
[ -e "$WORK" ] && { echo "work dir exists: $WORK"; exit 2; }
case "$V" in
  g262T) BASE_SH="$ROOT/port-j274/gpu/hv/tools-hv2/build-g262R.sh"; SRC=src-g262R; BBIN=build-g262R/build/m1n1.bin
         BSHA=83add623acbd7658449e84b20d664205ac085ae92e841bf0fab47c5554af89d5
         PATCH="$HERE/hv2-g262R-to-g262T.patch"; PSHA=48c769ea7753f38d58d94b91ed180a99274663b3946f260d4438b32e7e9a8e6a
         WANT=d5ee4781920a13993d186f56f3f37d8ff2d600a5e40b5e7d34f26239a31660af ;;
  s264H) BASE_SH="$ROOT/nwoas_scripts/standalone-s264/build-s264g.sh"; SRC=src-s264G; BBIN=build-s264G/build/m1n1.bin
         BSHA=d1fea3d73ad7d0d7da562802dbdec88527490dfe5f395d28dbd464a324c43ce6
         PATCH="$HERE/hv2-s264G-to-s264H.patch"; PSHA=f3d51d76082683b1cb7bc812e86ad810e2410b3ff5fff4a067c68d6dba9ff0da
         WANT=e5070a2e3268a04cb0571c96e4ea2ee382efdcf154c6699ee291a0c9ff466949 ;;
  *) echo "usage: build-pmu.sh g262T|s264H <new work dir>"; exit 2 ;;
esac
[ "$(shasum -a 256 "$PATCH" | awk '{print $1}')" = "$PSHA" ] || { echo "STOP: $(basename "$PATCH") differs from the recorded one"; exit 2; }
B="${BASE_WORK:-$WORK/base}"
[ -n "${BASE_WORK:-}" ] || bash "$BASE_SH" "$B"
[ "$(shasum -a 256 "$B/$BBIN" | awk '{print $1}')" = "$BSHA" ] || { echo "STOP: base binary is not the recorded one"; exit 2; }
mkdir -p "$WORK"
rsync -a --exclude build "$B/$SRC/" "$WORK/src-$V/"
( cd "$WORK/src-$V" && patch -p1 -s --no-backup-if-mismatch < "$PATCH" )
cc -std=gnu11 -Wall -Wextra -I"$WORK/src-$V/src" "$HERE/hosttest/test_pmu_filter.c" -o "$WORK/test_pmu_filter"
"$WORK/test_pmu_filter"
bash "$ROOT/port-j274/gpu/hv/tools-hv2/buildvar.sh" "$WORK/src-$V" "$WORK/build-$V" "$V" NWOAS_AGX=1
BIN="$WORK/build-$V/build/m1n1.bin"
python3 -I - "$BIN" <<'PY'
import sys, struct
d = open(sys.argv[1], 'rb').read(); w = struct.unpack('<%dI' % (len(d) // 4), d[:len(d) // 4 * 4])
n = lambda b: sum(1 for x in w if (x & ~0x1f) == b)
print('PMCR1_EL12 (s3_1_c15_c7_2): mrs %d msr %d' % (n(0xD539F740), n(0xD519F740)))
sys.exit(0 if n(0xD519F740) >= 1 else 1)
PY
echo "$V: $(stat -f %z "$BIN") B sha256 $(shasum -a 256 "$BIN" | awk '{print $1}')"
[ "$(shasum -a 256 "$BIN" | awk '{print $1}')" = "$WANT" ] && echo "$V reproduced: $WANT" || { echo "$V differs from the recorded one"; exit 1; }
