#!/bin/bash
# S262: rebuild m1n1-g262P = m1n1-gpu-hv2 (g260O source) + ../hv2-g260O-to-g262P.patch,
# run the host test of the concurrent NVMe path, and compare with the recorded binary.
# usage: build-g262P.sh <new work dir>     (refuses an existing dir; installs nothing)
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
WORK="${1:?usage: build-g262P.sh <new work dir>}"
[ -e "$WORK" ] && { echo "work dir exists: $WORK"; exit 2; }
EXPECT_SHA=c89fc27402f4f576dbd8f5f3590869bab540c12c722bd3abc846fc09efab6069
EXPECT_SIZE=2326528
# g260O base files the patch applies to (anything else changed = stop, do not guess)
( cd "$ROOT/m1n1-gpu-hv2/src" && shasum -a 256 -c - ) <<'EOF'
a7c535317516027cc45361085880efef86d605e2ebd94865f3b18847052798c8  hv_exc.c
b90c7b9b583b949fa9c54c22563745a3bfefbfd2d77eea7efe167321359976f2  hv_vm.c
23073b85c2130649dfed3f4bd25c91adf32b919a5db489778e73d69e794cae13  nvme.c
cce2667a7eded4a1be6c0c5e6321eaf0158d0a2195c856eb69bfe59e7e47e198  nvme.h
EOF
[ ! -e "$ROOT/m1n1-gpu-hv2/src/nwoas_nvmeqd.inc" ] || { echo 'base already has nwoas_nvmeqd.inc'; exit 2; }
mkdir -p "$WORK"
rsync -a --exclude build "$ROOT/m1n1-gpu-hv2/" "$WORK/src-g262P/"
( cd "$WORK/src-g262P" && patch -p1 -s --no-backup-if-mismatch < "$HERE/../hv2-g260O-to-g262P.patch" )
clang -std=gnu11 -O1 -Wall -Wno-unused-function -Wno-format -I"$WORK/src-g262P/src" \
    "$HERE/hosttest/test_nvmeqd.c" -o "$WORK/test_nvmeqd"
"$WORK/test_nvmeqd"
bash "$HERE/buildvar.sh" "$WORK/src-g262P" "$WORK/build-g262P" g262P NWOAS_AGX=1
BIN="$WORK/build-g262P/build/m1n1.bin"
[ "$(stat -f %z "$BIN")" = "$EXPECT_SIZE" ] && [ "$(shasum -a 256 "$BIN" | awk '{print $1}')" = "$EXPECT_SHA" ] \
    && echo "g262P reproduced: $EXPECT_SHA" || { echo 'g262P binary differs from the recorded one'; exit 1; }
