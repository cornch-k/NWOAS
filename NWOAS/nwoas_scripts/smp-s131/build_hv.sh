#!/bin/zsh
set -euo pipefail

ROOT=/Volumes/X31/NWOAS
M1N1="$ROOT/m1n1_windows"
OUT="$ROOT/nwoas_scripts/smp-s131"

cd "$M1N1"
make -j"$(sysctl -n hw.logicalcpu)"
cp build/m1n1.bin build/m1n1-s131-psci-index.bin
shasum -a 256 build/m1n1-s131-psci-index.bin > "$OUT/hv.sha256"
stat -f 'bytes=%z path=%N' build/m1n1-s131-psci-index.bin
cat "$OUT/hv.sha256"
