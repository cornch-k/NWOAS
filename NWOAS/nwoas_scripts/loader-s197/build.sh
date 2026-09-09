#!/bin/bash
# S197: offline rebuild of the GUEST m1n1 prefix from known source.
#
# Source : detached worktree /Volumes/X31/NWOAS/m1n1-guest-s197 at bddf7f06
#          (m1n1_windows HEAD, branch nwoas-stage8-instrumentation). The worktree
#          must be clean; the three uncommitted host-side files in m1n1_windows
#          (proxyclient/tools/run_guest.py, src/hv_vm.c, src/nwoas_stage8.inc)
#          are intentionally NOT part of this build.
# Variant: head   = tracked source exactly as committed (default)
#          compat = head + guest-compat-s197.patch (reverts the three post-July
#                   guest-visible hunks in src/main.c, src/i2c.c, src/usb.c to
#                   their 59fb544 form; see README). The patch is applied for the
#                   duration of the build only and the worktree is restored.
# Tools  : Homebrew LLVM/LLD 20.1.8 (explicit paths, no brew lookup),
#          Rust 1.88.0 via RUSTUP_TOOLCHAIN (no default toolchain is set on this
#          host), cargo offline (uuid 1.17.0 is in ~/.cargo/registry cache,
#          rust-fatfs is the vendored submodule at 4eccb50d, verified per file
#          by nwoas_scripts/loader-s199/verify_vendor_tree.sh before building).
# Load   : nice -n 19, make -j2, CARGO_BUILD_JOBS=2.
# Output : nwoas_scripts/loader-s197/out/<variant>/{m1n1.bin,m1n1-raw.elf,
#          m1n1.macho,m1n1.elf,build_tag.h,build_cfg.h,build.log,tools.txt,
#          sha256.txt}
#
# This script never touches m1n1_windows, m1n1_windows-s159, hardware, USB or
# any launcher. It does not boot anything.
set -euo pipefail

ROOT=/Volumes/X31/NWOAS
WT="$ROOT/m1n1-guest-s197"
HERE="$ROOT/nwoas_scripts/loader-s197"
VARIANT="${1:-head}"
OUT="$HERE/out/$VARIANT"

EXPECT_COMMIT=bddf7f06f033a7411834ac61381d8c997034f532
EXPECT_FATFS=4eccb50d011146fbed20e133d33b22f3c27292e7
LLVM=/opt/homebrew/opt/llvm/bin/
LLD=/opt/homebrew/opt/lld/bin/
RUST_TC=1.88.0

case "$VARIANT" in head|compat) ;; *) echo "usage: $0 [head|compat]"; exit 2 ;; esac

# --- source pinning -------------------------------------------------------
[ "$(git -C "$WT" rev-parse HEAD)" = "$EXPECT_COMMIT" ] || { echo "STOP: worktree not at $EXPECT_COMMIT"; exit 1; }
[ -z "$(git -C "$WT" status --porcelain --untracked-files=no)" ] || { echo "STOP: worktree has tracked modifications"; git -C "$WT" status --short; exit 1; }
[ "$(git -C "$WT" ls-tree HEAD rust/vendor/rust-fatfs | awk '{print $3}')" = "$EXPECT_FATFS" ] || { echo "STOP: rust-fatfs gitlink mismatch"; exit 1; }
[ -f "$WT/rust/vendor/rust-fatfs/Cargo.toml" ] || { echo "STOP: rust-fatfs tree not populated (git archive $EXPECT_FATFS into it)"; exit 1; }
# Vendored tree must be byte-identical to the pinned commit. S199: this is a
# per-file blob-hash comparison (content, executable bit, symlink/regular type,
# no extra or missing paths) against the tree object in the main checkout's
# submodule store; read-only, nothing is written to either repository. The
# previous check only diffed the file *lists*, which cannot see edits.
VERIFY="$ROOT/nwoas_scripts/loader-s199/verify_vendor_tree.sh"
[ -x "$VERIFY" ] || { echo "STOP: $VERIFY missing or not executable"; exit 1; }
"$VERIFY" "$WT/rust/vendor/rust-fatfs" "$ROOT/m1n1_windows/rust/vendor/rust-fatfs" "$EXPECT_FATFS" \
  || { echo "STOP: rust-fatfs tree is not byte-identical to $EXPECT_FATFS"; exit 1; }

# --- tool pinning ---------------------------------------------------------
[ -x "${LLVM}clang" ] && [ -x "${LLD}ld.lld" ] || { echo "STOP: LLVM/LLD missing"; exit 1; }
"${LLVM}clang" --version | grep -q "clang version 20.1.8" || { echo "STOP: clang is not 20.1.8"; exit 1; }
"${LLD}ld.lld" --version | grep -q "LLD 20.1.8" || { echo "STOP: ld.lld is not 20.1.8"; exit 1; }
export RUSTUP_TOOLCHAIN="$RUST_TC"
rustc --version | grep -q "rustc 1.88.0" || { echo "STOP: rustc is not 1.88.0"; exit 1; }
rustup target list --installed | grep -q '^aarch64-unknown-none-softfloat$' || { echo "STOP: softfloat target missing for $RUST_TC"; exit 1; }
export CARGO_NET_OFFLINE=true CARGO_BUILD_JOBS=2

mkdir -p "$OUT"
{
  echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "variant: $VARIANT"
  echo "commit: $(git -C "$WT" rev-parse HEAD)"
  echo "describe: $(git -C "$WT" describe --tags --always --dirty)"
  echo "rust-fatfs: $EXPECT_FATFS"
  echo "artwork gitlink: $(git -C "$WT" ls-tree HEAD artwork | awk '{print $3}') (not needed: data/*.bin are tracked)"
  "${LLVM}clang" --version | head -1
  "${LLD}ld.lld" --version
  "${LLVM}llvm-objcopy" --version | head -2 | tr '\n' ' '; echo
  rustc --version; cargo --version
  make --version | head -1
  echo "make flags: TOOLCHAIN=$LLVM LLDDIR=$LLD USE_CLANG=1 -j2 (nice 19)"
  echo "CFG (Makefile): RELEASE unset, CHAINLOADING unset, BUILDSTD unset, LOGO unset"
  if [ "$VARIANT" = compat ]; then echo "patch: guest-compat-s197.patch sha256 $(shasum -a 256 "$HERE/guest-compat-s197.patch" | awk '{print $1}')"; fi
} > "$OUT/tools.txt"

restore() {
  if [ "$VARIANT" = compat ]; then
    git -C "$WT" checkout -- src/main.c src/i2c.c src/usb.c
  fi
}
trap restore EXIT

if [ "$VARIANT" = compat ]; then
  git -C "$WT" apply "$HERE/guest-compat-s197.patch"
  # The build tag must say what was built.
  export M1N1_VERSION_TAG="$(git -C "$WT" describe --tags --always)-s197compat"
fi

# Always start from an empty build dir so the artifact reflects only this source.
rm -rf "$WT/build"
( cd "$WT" && nice -n 19 make TOOLCHAIN="$LLVM" LLDDIR="$LLD" USE_CLANG=1 -j2 ) > "$OUT/build.log" 2>&1 \
  || { echo "BUILD FAILED, see $OUT/build.log"; tail -30 "$OUT/build.log"; exit 1; }

cp "$WT/build/m1n1.bin" "$WT/build/m1n1-raw.elf" "$WT/build/m1n1.macho" "$WT/build/m1n1.elf" \
   "$WT/build/build_tag.h" "$WT/build/build_cfg.h" "$OUT/"
( cd "$OUT" && shasum -a 256 m1n1.bin m1n1-raw.elf m1n1.macho m1n1.elf > sha256.txt )
echo "built $VARIANT:"; cat "$OUT/build_tag.h"; cat "$OUT/sha256.txt"; stat -f '%z bytes m1n1.bin' "$OUT/m1n1.bin"
