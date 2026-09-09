#!/bin/bash
# S208: bounded NWOAS NVMe diagnostic-correctness rebuild.
#
# Candidate source = detached worktree /Volumes/X31/NWOAS/m1n1-diag-s208 at
# bddf7f06 + the exact S163 companion patch
# (nwoas_scripts/publish-s198/companion/m1n1_windows.patch) + one call-site fix:
# in src/hv_exc.c the enabled-vGIC hv_exc_irq EOI note now reads hv_get_elr()
# instead of the never-initialized ctx->elr. Nothing else changes.
#
# Two pinned binaries are produced, differing ONLY in NWOAS_NVME_REASSERT_US:
#   gap50  -DNWOAS_NVME_REASSERT_US=50   (matches the S163 tested gap50 flags)
#   gap25  -DNWOAS_NVME_REASSERT_US=25
# Both carry -DNWOAS_NVME_MAX_BLOCKS=256. Load: nice -n 19, make -j2,
# CARGO_BUILD_JOBS=2, cargo offline.
#
# Never writes m1n1_windows, m1n1_windows-s159, m1n1-guest-s197, m1n1-rebuild-s205,
# hardware, USB or any launcher. Build dir is wiped before each variant.
set -euo pipefail

ROOT=/Volumes/X31/NWOAS
WT="$ROOT/m1n1-diag-s208"
HERE="$ROOT/nwoas_scripts/nvme-s208"
OUT="$HERE/out"

EXPECT_COMMIT=bddf7f06f033a7411834ac61381d8c997034f532
EXPECT_FATFS=4eccb50d011146fbed20e133d33b22f3c27292e7
FATFS_STORE="$ROOT/m1n1_windows/.git/modules/rust/vendor/rust-fatfs"
PATCH="$ROOT/nwoas_scripts/publish-s198/companion/m1n1_windows.patch"
EXPECT_PATCH=a3f6c5741f1d96db30ee88d3e2148763214d582ddcb50b67c080834e70b65851
VERIFY="$ROOT/nwoas_scripts/loader-s199/verify_vendor_tree.sh"
LLVM=/opt/homebrew/opt/llvm/bin/
LLD=/opt/homebrew/opt/lld/bin/
RUST_TC=1.88.0
MAXB=256

sha() { shasum -a 256 "$1" | awk '{print $1}'; }

# --- source pinning -------------------------------------------------------
[ "$(git -C "$WT" rev-parse HEAD)" = "$EXPECT_COMMIT" ] || { echo "STOP: worktree not at $EXPECT_COMMIT"; exit 1; }
[ "$(sha "$PATCH")" = "$EXPECT_PATCH" ] || { echo "STOP: companion patch sha256 mismatch"; exit 1; }
[ "$(git -C "$WT" ls-tree HEAD rust/vendor/rust-fatfs | awk '{print $3}')" = "$EXPECT_FATFS" ] || { echo "STOP: rust-fatfs gitlink mismatch"; exit 1; }
[ -x "$VERIFY" ] || { echo "STOP: $VERIFY missing"; exit 1; }

# The candidate diff (companion + the one-line fix) must be exactly the four
# expected files and the fix must be present.
CHANGED="$(git -C "$WT" status --porcelain --untracked-files=no | awk '{print $2}' | sort | tr '\n' ' ')"
EXPECT_CHANGED="proxyclient/tools/run_guest.py src/hv_exc.c src/hv_vm.c src/nwoas_stage8.inc "
[ "$CHANGED" = "$EXPECT_CHANGED" ] || { echo "STOP: unexpected working-tree changes: [$CHANGED]"; exit 1; }
grep -q "nwoas_nvme_note_eoi(intd, hv_get_elr());" "$WT/src/hv_exc.c" || { echo "STOP: S208 fix missing"; exit 1; }
! grep -q "nwoas_nvme_note_eoi(intd, ctx->elr);" "$WT/src/hv_exc.c" || { echo "STOP: pre-fix line still present"; exit 1; }

# Populate vendored rust-fatfs from the pinned commit if empty (no network).
if [ -z "$(ls -A "$WT/rust/vendor/rust-fatfs" 2>/dev/null)" ]; then
  git -C "$FATFS_STORE" archive --format=tar "$EXPECT_FATFS" | tar -x -C "$WT/rust/vendor/rust-fatfs"
fi
"$VERIFY" "$WT/rust/vendor/rust-fatfs" "$FATFS_STORE" "$EXPECT_FATFS" \
  || { echo "STOP: rust-fatfs tree not byte-identical to $EXPECT_FATFS"; exit 1; }

# --- tool pinning ---------------------------------------------------------
"${LLVM}clang" --version | grep -q "clang version 20.1.8" || { echo "STOP: clang not 20.1.8"; exit 1; }
"${LLD}ld.lld" --version | grep -q "LLD 20.1.8" || { echo "STOP: ld.lld not 20.1.8"; exit 1; }
export RUSTUP_TOOLCHAIN="$RUST_TC"
rustc --version | grep -q "rustc 1.88.0" || { echo "STOP: rustc not 1.88.0"; exit 1; }
export CARGO_NET_OFFLINE=true CARGO_BUILD_JOBS=2
unset M1N1_VERSION_TAG

mkdir -p "$OUT"
TAG_NOW="$(git -C "$WT" describe --tags --always --dirty)"

build_variant() {
  local gap="$1"
  local name="m1n1-s208-eoipc-gap${gap}"
  local cflags="-DNWOAS_NVME_MAX_BLOCKS=${MAXB} -DNWOAS_NVME_REASSERT_US=${gap}"
  echo ">>> building gap${gap}: EXTRA_CFLAGS='$cflags'"
  rm -rf "$WT/build"
  ( cd "$WT" && nice -n 19 make TOOLCHAIN="$LLVM" LLDDIR="$LLD" USE_CLANG=1 \
      EXTRA_CFLAGS="$cflags" -j2 ) > "$OUT/build-gap${gap}.log" 2>&1 \
    || { echo "BUILD FAILED gap${gap}, see $OUT/build-gap${gap}.log"; tail -30 "$OUT/build-gap${gap}.log"; exit 1; }
  cp "$WT/build/m1n1.bin"      "$OUT/${name}.bin"
  cp "$WT/build/m1n1-raw.elf"  "$OUT/${name}-raw.elf"
  cp "$WT/build/m1n1.macho"    "$OUT/${name}.macho"
  cp "$WT/build/m1n1.elf"      "$OUT/${name}.elf"
  cp "$WT/build/build_tag.h"   "$OUT/${name}.build_tag.h"
  cp "$WT/build/build_cfg.h"   "$OUT/${name}.build_cfg.h"
}

build_variant 50
build_variant 25

( cd "$OUT" && shasum -a 256 m1n1-s208-eoipc-gap50.bin m1n1-s208-eoipc-gap50-raw.elf \
    m1n1-s208-eoipc-gap50.macho m1n1-s208-eoipc-gap50.elf \
    m1n1-s208-eoipc-gap25.bin m1n1-s208-eoipc-gap25-raw.elf \
    m1n1-s208-eoipc-gap25.macho m1n1-s208-eoipc-gap25.elf > sha256.txt )

{
  echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "worktree: $WT"
  echo "base_commit: $EXPECT_COMMIT"
  echo "describe (patched): $TAG_NOW"
  echo "companion_patch: $PATCH ($EXPECT_PATCH)"
  echo "s208_fix: src/hv_exc.c hv_exc_irq EOI note ctx->elr -> hv_get_elr()"
  echo "rust_fatfs: $EXPECT_FATFS"
  echo "MAX_BLOCKS: $MAXB"
  "${LLVM}clang" --version | head -1
  "${LLD}ld.lld" --version
  rustc --version; cargo --version; make --version | head -1
  echo "load: nice -n 19, make -j2, CARGO_BUILD_JOBS=2, CARGO_NET_OFFLINE=true"
} > "$OUT/tools.txt"

echo "=== build_tag ==="; cat "$OUT/m1n1-s208-eoipc-gap50.build_tag.h"
echo "=== sha256 ==="; cat "$OUT/sha256.txt"
