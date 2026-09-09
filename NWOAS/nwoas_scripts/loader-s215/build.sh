#!/bin/bash
set -euo pipefail
ROOT=/Volumes/X31/NWOAS
HERE="$ROOT/nwoas_scripts/loader-s215"
WT="$ROOT/m1n1-guest-s215"
mkdir -p "$HERE/out"
/opt/homebrew/opt/llvm/bin/clang --version | grep -q 'clang version 20.1.8'
/opt/homebrew/opt/lld/bin/ld.lld --version | grep -q 'LLD 20.1.8'
for optvar in EXTRA_CFLAGS RELEASE CHAINLOADING LOGO BUILDSTD; do
 [ -z "${!optvar:-}" ] || { echo "STOP: unexpected build override $optvar"; exit 1; }
done
[ "$(git -C "$WT" rev-parse HEAD)" = bddf7f06f033a7411834ac61381d8c997034f532 ]
[ -z "$(git -C "$WT" status --porcelain --untracked-files=no)" ]
if [ ! -f "$WT/rust/vendor/rust-fatfs/Cargo.toml" ]; then
 git -C "$ROOT/m1n1_windows/rust/vendor/rust-fatfs" archive 4eccb50d011146fbed20e133d33b22f3c27292e7 | tar -x -C "$WT/rust/vendor/rust-fatfs"
fi
"$ROOT/nwoas_scripts/loader-s199/verify_vendor_tree.sh" "$WT/rust/vendor/rust-fatfs" "$ROOT/m1n1_windows/rust/vendor/rust-fatfs" 4eccb50d011146fbed20e133d33b22f3c27292e7
restore() { git -C "$WT" restore src/main.c src/i2c.c src/usb.c src/payload.c; }
trap restore EXIT
cp "$HERE/handoff_layout.h" "$WT/src/nwoas_handoff_layout.h"
git -C "$WT" apply "$ROOT/nwoas_scripts/loader-s197/guest-compat-s197.patch"
git -C "$WT" apply "$HERE/placement.patch"
cp "$WT/src/payload.c" "$HERE/payload-s215.c"
export RUSTUP_TOOLCHAIN=1.88.0 CARGO_NET_OFFLINE=true CARGO_BUILD_JOBS=2 M1N1_VERSION_TAG=v1.0.2-1474-gbddf7f06-s215handoff
/opt/homebrew/opt/llvm/bin/clang --version | head -1 > "$HERE/out/tools.txt"
/opt/homebrew/opt/lld/bin/ld.lld --version >> "$HERE/out/tools.txt"
rustc --version >> "$HERE/out/tools.txt"
rm -rf "$WT/build"
(cd "$WT" && nice -n 19 make TOOLCHAIN=/opt/homebrew/opt/llvm/bin/ LLDDIR=/opt/homebrew/opt/lld/bin/ USE_CLANG=1 -j2) > "$HERE/out/build.log" 2>&1
cp "$WT/build/m1n1.bin" "$WT/build/m1n1-raw.elf" "$WT/build/build_tag.h" "$HERE/out/"
shasum -a 256 "$HERE/out/m1n1.bin"
