#!/bin/bash
# Reproduce S159 candidate with the physical limit matching advertised MDTS.
set -eu
ROOT=/Volumes/X31/NWOAS
TREE="$ROOT/m1n1_windows-s159"
export RUSTUP_TOOLCHAIN=1.88.0-aarch64-apple-darwin
cd "$TREE"
# make does not track changes to EXTRA_CFLAGS; force this object every time.
nice -n 10 make -B -j2 EXTRA_CFLAGS=-DNWOAS_NVME_MAX_BLOCKS=256 build/nvme.o
nice -n 10 make -j2 EXTRA_CFLAGS=-DNWOAS_NVME_MAX_BLOCKS=256 build/m1n1.bin
python3 "$ROOT/nwoas_scripts/nvme-s159/test_submission_defer.py" "$TREE/src/hv_vm.c"
cp build/m1n1.bin build/m1n1-s159-submit-deferred-256.bin
shasum -a 256 build/m1n1-s159-submit-deferred-256.bin src/hv_vm.c src/nvme.c
