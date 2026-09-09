#!/bin/sh
# Build and run the host tests for NwoasRtcSeedCore: plain, then ASan+UBSan.
# Third pass (best effort) cross-checks against the tree's TimeBaseLib.c by
# compiling it read-only with MdePkg headers; skipped if that fails.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
LIB="$HERE/NwoasSeedRealTimeClockLib"
OUT="$HERE/build"
MU="/Volumes/X31/NWOAS/apple_silicon_platforms_mu"
CC=${CC:-cc}
mkdir -p "$OUT"

WARN="-std=c99 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Werror"
SRC="$LIB/NwoasRtcSeedCore.c $HERE/tests/test_rtc_core.c"

echo "== plain -O2"
$CC $WARN -O2 -I"$LIB" $SRC -o "$OUT/test_plain"
"$OUT/test_plain"

echo "== ASan + UBSan -O1"
$CC $WARN -O1 -g -fno-omit-frame-pointer \
  -fsanitize=address,undefined -fno-sanitize-recover=all \
  -I"$LIB" $SRC -o "$OUT/test_asan"
UBSAN_OPTIONS=print_stacktrace=1 ASAN_OPTIONS=detect_leaks=0 "$OUT/test_asan"

echo "== core compiled as C11 with -Wc++-compat and as C++ (header hygiene)"
$CC -std=c11 -Wall -Wextra -Wc++-compat -Werror -c -I"$LIB" "$LIB/NwoasRtcSeedCore.c" -o "$OUT/core_c11.o"
$CC -x c++ -std=c++17 -Wall -Wextra -Werror -c -I"$LIB" "$LIB/NwoasRtcSeedCore.c" -o "$OUT/core_cxx.o"

echo "== TimeBaseLib cross-check (read-only compile of tree source; best effort)"
TBL="$MU/Common/TIANO/EmbeddedPkg/Library/TimeBaseLib/TimeBaseLib.c"
if [ -f "$TBL" ] && $CC -std=c99 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
     -DNWOAS_HAVE_TIMEBASELIB -DMDEPKG_NDEBUG -fshort-wchar \
     -I"$LIB" -I"$MU/MU_BASECORE/MdePkg/Include" -I"$MU/MU_BASECORE/MdePkg/Include/AArch64" \
     -I"$MU/Common/TIANO/EmbeddedPkg/Include" \
     -Wno-unused-parameter -w \
     "$LIB/NwoasRtcSeedCore.c" "$HERE/tests/test_rtc_core.c" "$TBL" -o "$OUT/test_tbl" 2>"$OUT/tbl_build.log"; then
  ASAN_OPTIONS=detect_leaks=0 "$OUT/test_tbl"
else
  echo "TimeBaseLib cross-check SKIPPED (see $OUT/tbl_build.log)"
fi

echo "== syntax-only compile of the UEFI library against tree headers (no firmware build)"
if $CC -fsyntax-only -std=c11 -w -target arm64-none-elf -ffreestanding -fshort-wchar \
     -I"$LIB" -I"$MU/MU_BASECORE/MdePkg/Include" -I"$MU/MU_BASECORE/MdePkg/Include/AArch64" \
     -I"$MU/MU_BASECORE/MdeModulePkg/Include" -I"$MU/Silicon/ARM/TIANO/ArmPkg/Include" \
     -I"$MU/Silicon/Apple/AppleSiliconPkg/Include" -I"$MU/Common/TIANO/EmbeddedPkg/Include" \
     "$LIB/NwoasSeedRealTimeClockLib.c" 2>"$OUT/uefi_syntax.log"; then
  echo "UEFI library syntax check OK"
else
  echo "UEFI library syntax check FAILED (see $OUT/uefi_syntax.log)"
  exit 1
fi
echo "ALL DONE"
