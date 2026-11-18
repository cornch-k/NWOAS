#!/bin/bash
# Build and run the host unit tests for the hardware independent driver core.
set -eu
cd "$(dirname "$0")"
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
CFLAGS="-std=c99 -O2 -Wall -Wextra -Werror -Wconversion -Wno-sign-conversion -fsanitize=address,undefined"
clang $CFLAGS -o "$OUT/test_nwau_core" test_nwau_core.c ../Source/Main/nwau_core.c
"$OUT/test_nwau_core"
