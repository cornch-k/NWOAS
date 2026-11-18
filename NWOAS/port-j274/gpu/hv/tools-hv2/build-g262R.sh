#!/bin/bash
# S262 g262R: rebuild m1n1-g262R = m1n1-gpu-hv2 (g260O source) + ../hv2-g260O-to-g262P.patch
#   + ../hv2-g262P-to-g262Q.patch + ../hv2-g262Q-to-g262R.patch,
# run the host tests (concurrent NVMe path with the g262R rules; ANS IRQ choice on the real J274
# 13.5 ADT dump), and compare with the recorded binary.
# usage: build-g262R.sh <new work dir>     (refuses an existing dir; installs nothing)
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
WORK="${1:?usage: build-g262R.sh <new work dir>}"
[ -e "$WORK" ] && { echo "work dir exists: $WORK"; exit 2; }
EXPECT_SHA=${EXPECT_SHA_OVERRIDE-83add623acbd7658449e84b20d664205ac085ae92e841bf0fab47c5554af89d5}
EXPECT_SIZE=${EXPECT_SIZE_OVERRIDE-2326528}
ADT="$ROOT/nwoas_scripts/agx-g1-s246/evidence/g0-adt-20261116-live1/adt.bin"
[ "$(shasum -a 256 "$ADT" | awk '{print $1}')" = 32e63706ee8816b9a731324340ea2206c683ce441c7c2beda09ab20c4e285c23 ]
# g260O base files the patches apply to (anything else changed = stop, do not guess)
( cd "$ROOT/m1n1-gpu-hv2/src" && shasum -a 256 -c - ) <<'EOS'
a7c535317516027cc45361085880efef86d605e2ebd94865f3b18847052798c8  hv_exc.c
b90c7b9b583b949fa9c54c22563745a3bfefbfd2d77eea7efe167321359976f2  hv_vm.c
23073b85c2130649dfed3f4bd25c91adf32b919a5db489778e73d69e794cae13  nvme.c
cce2667a7eded4a1be6c0c5e6321eaf0158d0a2195c856eb69bfe59e7e47e198  nvme.h
EOS
[ ! -e "$ROOT/m1n1-gpu-hv2/src/nwoas_nvmeqd.inc" ] || { echo 'base already has nwoas_nvmeqd.inc'; exit 2; }
mkdir -p "$WORK"
rsync -a --exclude build "$ROOT/m1n1-gpu-hv2/" "$WORK/src-g262R/"
( cd "$WORK/src-g262R" && patch -p1 -s --no-backup-if-mismatch < "$HERE/../hv2-g260O-to-g262P.patch" &&
  patch -p1 -s --no-backup-if-mismatch < "$HERE/../hv2-g262P-to-g262Q.patch" &&
  patch -p1 -s --no-backup-if-mismatch < "$HERE/../hv2-g262Q-to-g262R.patch" )
clang -std=gnu11 -O1 -Wall -Wno-unused-function -Wno-format -I"$WORK/src-g262R/src" \
    "$HERE/hosttest/test_nvmeqd_g262R.c" -o "$WORK/test_nvmeqd_g262R"
"$WORK/test_nvmeqd_g262R"
clang -std=gnu11 -O1 -Wall -I"$HERE/hosttest" -I"$WORK/src-g262R/src" \
    "$HERE/hosttest/test_ansirq_g262R.c" "$HERE/hosttest/adt_host.c" -o "$WORK/test_ansirq_g262R"
"$WORK/test_ansirq_g262R" "$ADT" 590
bash "$HERE/buildvar.sh" "$WORK/src-g262R" "$WORK/build-g262R" g262R NWOAS_AGX=1
BIN="$WORK/build-g262R/build/m1n1.bin"
[ "$(stat -f %z "$BIN")" = "$EXPECT_SIZE" ] && [ "$(shasum -a 256 "$BIN" | awk '{print $1}')" = "$EXPECT_SHA" ] \
    && echo "g262R reproduced: $EXPECT_SHA" || { echo 'g262R binary differs from the recorded one'; exit 1; }
