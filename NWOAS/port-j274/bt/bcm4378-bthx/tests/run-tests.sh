#!/bin/bash
# Build and run the host unit tests for the hardware independent Bluetooth core (ASan + UBSan).
# The macOS firmware files (Apple's, never copied anywhere here) are only opened to check they exist.
set -eu
cd "$(dirname "$0")"
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
CFLAGS="-std=c99 -O2 -Wall -Wextra -Werror -Wconversion -Wno-sign-conversion -fsanitize=address,undefined"
clang $CFLAGS -o "$OUT/test_nwbt_core" test_nwbt_core.c ../src/core/nwbt_core.c
FW=/usr/share/firmware/bluetooth
set --
for f in BCM4378B1_24.2.551.4114_PCIE_macOS_AtlantisB_OS_MUR_20260715.bin \
         BCM4378B1_FinalV0_PCIE_macOS_AtlantisB_OS_MUR_K_R_20200812.ptb; do
  [ -f "$FW/$f" ] && set -- "$@" "$FW/$f"
done
"$OUT/test_nwbt_core" "$@"
