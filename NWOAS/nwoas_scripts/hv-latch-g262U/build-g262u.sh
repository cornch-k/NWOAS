#!/bin/bash
# NWOAS g262U (host only; installs nothing, touches no device): m1n1-g262U = g262T (pmu-cyc/build-pmu.sh g262T)
# + hv2-g262T-to-g262U.patch (DCP reconnect: a present whose id the reconnect modeset replaced counts as latched;
# a failed latch no longer leaves the scanout broker refusing every later PRESENT; latch mismatches are logged).
#   bash build-g262u.sh <new work dir>        T_WORK=<existing work dir of build-pmu.sh g262T> skips rebuilding g262T
# Also runs hosttest/test_latch.c against the patched source.  The binary is NOT copied to the daily path
# (port-j274/gpu/hv/out-hv2); copy it to hv-latch-g262U/out by hand after checking the printed sha256.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
WORK="${1:?usage: build-g262u.sh <new work dir>}"
[ -e "$WORK" ] && { echo "work dir exists: $WORK"; exit 2; }
PATCH="$HERE/hv2-g262T-to-g262U.patch"; PSHA=a3cc35ad04896c213c0938f56a60493e40b05523464664aa61e20da42659b257
TSHA=d5ee4781920a13993d186f56f3f37d8ff2d600a5e40b5e7d34f26239a31660af
[ "$(shasum -a 256 "$PATCH" | awk '{print $1}')" = "$PSHA" ] || { echo "STOP: the patch differs from the recorded one"; exit 2; }
T="${T_WORK:-$WORK/g262T}"
[ -n "${T_WORK:-}" ] || bash "$ROOT/nwoas_scripts/pmu-cyc/build-pmu.sh" g262T "$T"
[ "$(shasum -a 256 "$T/build-g262T/build/m1n1.bin" | awk '{print $1}')" = "$TSHA" ] || { echo "STOP: g262T binary is not the recorded one"; exit 2; }
mkdir -p "$WORK"
rsync -a --exclude build "$T/src-g262T/" "$WORK/src-g262U/"
( cd "$WORK/src-g262U" && patch -p1 -s --no-backup-if-mismatch < "$PATCH" )
cc -std=gnu11 -Wall -Wextra -I"$WORK/src-g262U/src" "$HERE/hosttest/test_latch.c" "$WORK/src-g262U/src/hv_agx_scanout_broker.c" -o "$WORK/test_latch"
"$WORK/test_latch"
bash "$ROOT/port-j274/gpu/hv/tools-hv2/buildvar.sh" "$WORK/src-g262U" "$WORK/build-g262U" g262U NWOAS_AGX=1
BIN="$WORK/build-g262U/build/m1n1.bin"
strings -a "$BIN" | grep -c 'NWOAS-DCP-G262U' | sed 's/^/G262U log strings in the binary: /'
echo "g262U: $(stat -f %z "$BIN") B sha256 $(shasum -a 256 "$BIN" | awk '{print $1}')"
[ -z "${WANT:-}" ] || { [ "$(shasum -a 256 "$BIN" | awk '{print $1}')" = "$WANT" ] && echo "g262U reproduced: $WANT" || { echo "g262U differs from WANT"; exit 1; }; }
