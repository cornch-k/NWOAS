#!/bin/bash
# S175 host build: compile the helper freestanding (proves no libc/heap use),
# then build and run the fake-MMIO tests under ASan+UBSan, warning-clean.
set -eu
cd "$(dirname "$0")"
CC=${CC:-cc}
OUT=${OUT:-./build}
mkdir -p "$OUT"
WARN="-std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wstrict-prototypes -Werror"

echo "[1/3] freestanding compile of helper"
$CC $WARN -ffreestanding -fno-builtin -O2 -c s175_dart1_translate.c -o "$OUT/s175_dart1_translate.o"
if nm "$OUT/s175_dart1_translate.o" | grep -E ' U (_?malloc|_?memset|_?memcpy|_?free)$'; then
    echo "STOP: helper references libc/heap symbols"; exit 1
fi

echo "[2/3] ASan+UBSan test build"
$CC $WARN -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
    s175_dart1_translate.c test_s175.c -o "$OUT/test_s175"

echo "[3/3] run"
"$OUT/test_s175" | tee "$OUT/test_s175.log"
shasum -a 256 s175_dart1_translate.h s175_dart1_translate.c test_s175.c build.sh | tee "$OUT/manifest.sha256"
