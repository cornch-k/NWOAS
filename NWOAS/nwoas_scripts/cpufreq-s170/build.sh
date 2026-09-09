#!/bin/sh
# Build and run the S170 host tests for the header-only P-state helper.
#
# The REAL header (Include/Library/NwoasCpuPstate.h) is compiled via the same
# <Library/...> include path the firmware uses (-IInclude), against the stub
# <Base.h> in tests/stub (-Itests/stub). A green run proves the code branches as
# designed; it is NOT a boot proof.
set -e
cd "$(dirname "$0")"

CC="${CC:-cc}"
INCS="-IInclude -Itests/stub"
WARN="-std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -Wsign-conversion"
SRC="tests/test_nwoas_cpu_pstate.c"

echo "== build + run (plain, -O2) =="
"$CC" $WARN -O2 $INCS "$SRC" -o tests/test_plain
./tests/test_plain

echo
echo "== build + run (ASan/UBSan, if available) =="
if "$CC" $WARN -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
      $INCS "$SRC" -o tests/test_san 2>/dev/null; then
  ASAN_OPTIONS=detect_leaks=0 ./tests/test_san
else
  echo "sanitizers unavailable with $CC; skipped"
fi

rm -f tests/test_plain tests/test_san
rm -rf tests/test_plain.dSYM tests/test_san.dSYM
echo
echo "OK"
