#!/bin/bash
# Build and run the host unit tests for the hardware independent Wi-Fi core.
# The real-NVRAM check compares against the Python converter that passed on the
# Mac mini (nwoas_scripts/wifi-s257/wifi_up.py convert_nvram); the macOS NVRAM file
# and its conversion only live in a temp dir.
set -eu
cd "$(dirname "$0")"
ROOT=$(cd ../../../.. && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
CFLAGS="-std=c99 -O2 -Wall -Wextra -Werror -Wconversion -Wno-sign-conversion -fsanitize=address,undefined"
clang $CFLAGS -o "$OUT/test_nwbc_core" test_nwbc_core.c ../src/core/nwbc_core.c
NV=/usr/share/firmware/wifi/C-4378__s-B1/P-atlantisb_M-RASP_V-m__m-6.11.txt
if [ -f "$NV" ]; then
  python3 -I - "$ROOT" "$NV" "$OUT/nvram-py.bin" <<'EOF'
import sys
root, nv, out = sys.argv[1:4]
sys.path.insert(0, root + '/nwoas_scripts/wifi-s240')
sys.path.insert(0, root + '/nwoas_scripts/wifi-s257')
import wifi_up
blob, _ = wifi_up.convert_nvram(open(nv, 'rb').read(), bytes([2, 0, 0, 0, 0, 1]))
open(out, 'wb').write(blob)
EOF
  "$OUT/test_nwbc_core" "$NV" "$OUT/nvram-py.bin"
else
  "$OUT/test_nwbc_core"
fi
