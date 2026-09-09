#!/bin/sh
# S185 review tests. Compiles the REAL, unmodified library sources from
# ../NwoasHardwareBootRtcLib/ against host shim headers and runs the
# integration test plain (-O2) and under ASan+UBSan. Also rebuilds and runs
# the existing S181 core and S184 helper suites from their unmodified sources
# into this directory's build/ folder (their own build/ folders are untouched).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
LIB="$HERE/../NwoasHardwareBootRtcLib"
S181="$HERE/../../rtc-s181"
S184="$HERE/../../rtc-s184"
OUT="$HERE/build"
CC=${CC:-cc}
mkdir -p "$OUT"

# -Wno-unused-parameter: the firmware build uses -Wall -Werror without -Wextra;
# the stub SetTime/wakeup/notify functions legitimately ignore their arguments.
WARN="-std=c11 -Wall -Wextra -Wno-unused-parameter -Wshadow -Wconversion -Wsign-conversion -Werror"
INC="-I$HERE/shim -I$LIB"
SRC="$LIB/NwoasHardwareBootRtcLib.c $LIB/NwoasRtcSeedCore.c $LIB/nwoas_sera_rtc.c $HERE/test_s185_integration.c"

echo "== integration: plain -O2"
$CC $WARN -O2 $INC $SRC -o "$OUT/s185_plain"
"$OUT/s185_plain"

echo "== integration: ASan + UBSan -O1"
$CC $WARN -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all \
  $INC $SRC -o "$OUT/s185_asan"
UBSAN_OPTIONS=print_stacktrace=1 ASAN_OPTIONS=detect_leaks=0 "$OUT/s185_asan"

echo "== S184 helper suite from unmodified sources (plain + ASan/UBSan)"
W184="-std=c99 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Werror"
$CC $W184 -O2 -I"$S184" "$S184/nwoas_sera_rtc.c" "$S184/tests/test_sera_rtc.c" -o "$OUT/s184_plain"
"$OUT/s184_plain" | tail -1
$CC $W184 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -I"$S184" \
  "$S184/nwoas_sera_rtc.c" "$S184/tests/test_sera_rtc.c" -o "$OUT/s184_asan"
ASAN_OPTIONS=detect_leaks=0 "$OUT/s184_asan" | tail -1

echo "== S181 core suite against the S185 copy of NwoasRtcSeedCore.c (plain + ASan/UBSan)"
$CC $W184 -O2 -I"$LIB" "$LIB/NwoasRtcSeedCore.c" "$S181/tests/test_rtc_core.c" -o "$OUT/s181_plain"
"$OUT/s181_plain" | tail -1
$CC $W184 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -I"$LIB" \
  "$LIB/NwoasRtcSeedCore.c" "$S181/tests/test_rtc_core.c" -o "$OUT/s181_asan"
ASAN_OPTIONS=detect_leaks=0 "$OUT/s181_asan" | tail -1

echo "ALL DONE"
