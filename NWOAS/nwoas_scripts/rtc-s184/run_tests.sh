#!/bin/sh
# Build and run the host tests for nwoas_sera_rtc: plain -O2, then ASan+UBSan,
# then header hygiene (C11 -Wc++-compat, C++), then a freestanding arm64
# syntax-only compile to prove the helper has no libc/UEFI dependency.
# Nothing here touches hardware or any file outside rtc-s184/.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT="$HERE/build"
CC=${CC:-cc}
mkdir -p "$OUT"

WARN="-std=c99 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Werror"
SRC="$HERE/nwoas_sera_rtc.c $HERE/tests/test_sera_rtc.c"

echo "== plain -O2"
$CC $WARN -O2 -I"$HERE" $SRC -o "$OUT/test_plain"
"$OUT/test_plain"

echo "== ASan + UBSan -O1"
$CC $WARN -O1 -g -fno-omit-frame-pointer \
  -fsanitize=address,undefined -fno-sanitize-recover=all \
  -I"$HERE" $SRC -o "$OUT/test_asan"
UBSAN_OPTIONS=print_stacktrace=1 ASAN_OPTIONS=detect_leaks=0 "$OUT/test_asan"

echo "== header hygiene: C11 -Wc++-compat and C++17"
$CC -std=c11 -Wall -Wextra -Wc++-compat -Werror -c -I"$HERE" "$HERE/nwoas_sera_rtc.c" -o "$OUT/core_c11.o"
$CC -x c++ -std=c++17 -Wall -Wextra -Werror -c -I"$HERE" "$HERE/nwoas_sera_rtc.c" -o "$OUT/core_cxx.o"

echo "== freestanding arm64 compile (no libc, no UEFI) and symbol audit"
$CC -std=c99 -O2 -Wall -Wextra -Werror -target arm64-none-elf -ffreestanding -nostdlib \
  -c -I"$HERE" "$HERE/nwoas_sera_rtc.c" -o "$OUT/core_arm64.o"
if nm -u "$OUT/core_arm64.o" | grep -q .; then
  echo "unexpected undefined symbols:"; nm -u "$OUT/core_arm64.o"; exit 1
fi
echo "no undefined symbols in freestanding object"
echo "ALL DONE"
