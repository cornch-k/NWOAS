#!/bin/bash
# S262: rebuild m1n1-g262Q = m1n1-gpu-hv2 (g260O source) + ../hv2-g260O-to-g262P.patch + ../hv2-g262P-to-g262Q.patch,
# run the host test of the concurrent NVMe path, and compare with the recorded binary.
# usage: build-g262Q.sh <new work dir>     (refuses an existing dir; installs nothing)
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
WORK="${1:?usage: build-g262Q.sh <new work dir>}"
[ -e "$WORK" ] && { echo "work dir exists: $WORK"; exit 2; }
EXPECT_SHA=6a1ce6d4519146ae66940a5296803e44cf8dee65710956322ba20e778cca25d2
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
rsync -a --exclude build "$ROOT/m1n1-gpu-hv2/" "$WORK/src-g262Q/"
( cd "$WORK/src-g262Q" && patch -p1 -s --no-backup-if-mismatch < "$HERE/../hv2-g260O-to-g262P.patch" &&
  patch -p1 -s --no-backup-if-mismatch < "$HERE/../hv2-g262P-to-g262Q.patch" )
clang -std=gnu11 -O1 -Wall -Wno-unused-function -Wno-format -I"$WORK/src-g262Q/src" \
    "$HERE/hosttest/test_nvmeqd.c" -o "$WORK/test_nvmeqd"
"$WORK/test_nvmeqd"
bash "$HERE/buildvar.sh" "$WORK/src-g262Q" "$WORK/build-g262Q" g262Q NWOAS_AGX=1
BIN="$WORK/build-g262Q/build/m1n1.bin"
[ "$(stat -f %z "$BIN")" = "$EXPECT_SIZE" ] && [ "$(shasum -a 256 "$BIN" | awk '{print $1}')" = "$EXPECT_SHA" ] \
    && echo "g262Q reproduced: $EXPECT_SHA" || { echo 'g262Q binary differs from the recorded one'; exit 1; }
