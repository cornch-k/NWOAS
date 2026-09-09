#!/bin/bash
# S205: independent, offline, CLEAN rebuild of the tested S163 gap50 host HV
# (m1n1_windows-s159/build/m1n1-s163-eoi-gap50.bin) from recorded source.
#
# Source : detached worktree /Volumes/X31/NWOAS/m1n1-rebuild-s205 at bddf7f06
#          plus the exact companion patch
#          nwoas_scripts/publish-s198/companion/m1n1_windows.patch
#          (sha256 pinned below, applied for the duration of the build and
#          reverted afterwards; the tracked tree is otherwise untouched).
# Flags  : EXTRA_CFLAGS='-DNWOAS_NVME_MAX_BLOCKS=256 -DNWOAS_NVME_REASSERT_US=50'
#          (mandatory; the S163 manifest records these for gap50).
# Tools  : Homebrew LLVM/LLD 20.1.8 (explicit paths), Rust 1.88.0 via
#          RUSTUP_TOOLCHAIN, cargo offline, target aarch64-unknown-none-softfloat.
#          rust-fatfs is populated from the pinned commit 4eccb50d in the local
#          submodule object store (git archive, no network) and verified per
#          file by nwoas_scripts/loader-s199/verify_vendor_tree.sh.
# Load   : nice -n 19, make -j2, CARGO_BUILD_JOBS=2.
# Output : nwoas_scripts/rebuild-s205/out/{m1n1.bin,m1n1-raw.elf,m1n1.macho,
#          m1n1.elf,build_tag.h,build_cfg.h,build.log,tools.txt,sha256.txt,
#          compare.txt,manifest.json}
#
# This script never writes to m1n1_windows, m1n1_windows-s159, m1n1-guest-s197,
# hardware, USB or any launcher, and never replaces the stable artifact.
# Reference for the pinning style: nwoas_scripts/loader-s197/build.sh (S197/S199).
set -euo pipefail

ROOT=/Volumes/X31/NWOAS
WT="$ROOT/m1n1-rebuild-s205"
HERE="$ROOT/nwoas_scripts/rebuild-s205"
OUT="$HERE/out"

EXPECT_COMMIT=bddf7f06f033a7411834ac61381d8c997034f532
EXPECT_FATFS=4eccb50d011146fbed20e133d33b22f3c27292e7
FATFS_STORE="$ROOT/m1n1_windows/.git/modules/rust/vendor/rust-fatfs"
PATCH="$ROOT/nwoas_scripts/publish-s198/companion/m1n1_windows.patch"
EXPECT_PATCH=a3f6c5741f1d96db30ee88d3e2148763214d582ddcb50b67c080834e70b65851
PATCH_FILES="proxyclient/tools/run_guest.py src/hv_exc.c src/hv_vm.c src/nwoas_stage8.inc"
STABLE="$ROOT/m1n1_windows-s159/build/m1n1-s163-eoi-gap50.bin"
EXPECT_STABLE=bd8f16f286c8d1141df4a17d9b85eafdd4c2d39d5680c6ed75b63429f061d166
EXPECT_STABLE_SIZE=2162688
EXTRA_CFLAGS='-DNWOAS_NVME_MAX_BLOCKS=256 -DNWOAS_NVME_REASSERT_US=50'
LLVM=/opt/homebrew/opt/llvm/bin/
LLD=/opt/homebrew/opt/lld/bin/
RUST_TC=1.88.0
VERIFY="$ROOT/nwoas_scripts/loader-s199/verify_vendor_tree.sh"

sha() { shasum -a 256 "$1" | awk '{print $1}'; }

# --- source pinning -------------------------------------------------------
[ -d "$WT/.git" ] || [ -f "$WT/.git" ] || { echo "STOP: $WT is not a git worktree"; exit 1; }
[ "$(git -C "$WT" rev-parse HEAD)" = "$EXPECT_COMMIT" ] || { echo "STOP: worktree not at $EXPECT_COMMIT"; exit 1; }
[ -z "$(git -C "$WT" status --porcelain --untracked-files=no)" ] || { echo "STOP: worktree has tracked modifications"; git -C "$WT" status --short; exit 1; }
[ "$(git -C "$WT" ls-tree HEAD rust/vendor/rust-fatfs | awk '{print $3}')" = "$EXPECT_FATFS" ] || { echo "STOP: rust-fatfs gitlink mismatch"; exit 1; }
[ "$(sha "$PATCH")" = "$EXPECT_PATCH" ] || { echo "STOP: companion patch sha256 mismatch"; exit 1; }
[ -x "$VERIFY" ] || { echo "STOP: $VERIFY missing or not executable"; exit 1; }
[ -f "$STABLE" ] || { echo "STOP: stable artifact $STABLE missing"; exit 1; }

# Populate the vendored rust-fatfs tree from the pinned commit in the local
# submodule store (no network, nothing written to any repository). git archive
# reproduces tree modes (100644/100755). Only done when the directory is empty.
if [ -z "$(ls -A "$WT/rust/vendor/rust-fatfs")" ]; then
  [ "$(git -C "$FATFS_STORE" cat-file -t "$EXPECT_FATFS")" = commit ] || { echo "STOP: $EXPECT_FATFS not in $FATFS_STORE"; exit 1; }
  git -C "$FATFS_STORE" archive --format=tar "$EXPECT_FATFS" | tar -x -C "$WT/rust/vendor/rust-fatfs"
  echo "populated rust/vendor/rust-fatfs from $EXPECT_FATFS"
fi
"$VERIFY" "$WT/rust/vendor/rust-fatfs" "$FATFS_STORE" "$EXPECT_FATFS" \
  || { echo "STOP: rust-fatfs tree is not byte-identical to $EXPECT_FATFS"; exit 1; }

# --- tool pinning ---------------------------------------------------------
[ -x "${LLVM}clang" ] && [ -x "${LLD}ld.lld" ] || { echo "STOP: LLVM/LLD missing"; exit 1; }
"${LLVM}clang" --version | grep -q "clang version 20.1.8" || { echo "STOP: clang is not 20.1.8"; exit 1; }
"${LLD}ld.lld" --version | grep -q "LLD 20.1.8" || { echo "STOP: ld.lld is not 20.1.8"; exit 1; }
export RUSTUP_TOOLCHAIN="$RUST_TC"
rustc --version | grep -q "rustc 1.88.0" || { echo "STOP: rustc is not 1.88.0"; exit 1; }
rustup target list --installed | grep -q '^aarch64-unknown-none-softfloat$' || { echo "STOP: softfloat target missing for $RUST_TC"; exit 1; }
export CARGO_NET_OFFLINE=true CARGO_BUILD_JOBS=2
unset M1N1_VERSION_TAG   # tag must come from `git describe --dirty`, as in the stable build

# --- apply the companion patch for the duration of the build ---------------
restore() { git -C "$WT" checkout -- $PATCH_FILES 2>/dev/null || true; }
trap restore EXIT
git -C "$WT" apply --check "$PATCH"
git -C "$WT" apply "$PATCH"
TAG_NOW="$(git -C "$WT" describe --tags --always --dirty)"

mkdir -p "$OUT"
{
  echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "worktree: $WT"
  echo "commit: $EXPECT_COMMIT"
  echo "describe (patched): $TAG_NOW"
  echo "patch: $PATCH"
  echo "patch sha256: $EXPECT_PATCH"
  echo "patch files: $PATCH_FILES"
  echo "rust-fatfs: $EXPECT_FATFS (tree $(git -C "$FATFS_STORE" rev-parse "$EXPECT_FATFS^{tree}"))"
  echo "artwork gitlink: $(git -C "$WT" ls-tree HEAD artwork | awk '{print $3}') (not needed: data/*.bin are tracked)"
  "${LLVM}clang" --version | head -1
  "${LLD}ld.lld" --version
  "${LLVM}llvm-objcopy" --version | head -2 | tr '\n' ' '; echo
  rustc --version; cargo --version
  make --version | head -1
  echo "make flags: TOOLCHAIN=$LLVM LLDDIR=$LLD USE_CLANG=1 EXTRA_CFLAGS='$EXTRA_CFLAGS' -j2 (nice 19), CARGO_BUILD_JOBS=2, CARGO_NET_OFFLINE=true"
  echo "CFG (Makefile): RELEASE unset, CHAINLOADING unset, BUILDSTD unset, LOGO unset"
  echo "stable reference: $STABLE sha256 $EXPECT_STABLE size $EXPECT_STABLE_SIZE"
} > "$OUT/tools.txt"

# Always start from an empty build dir so the artifact reflects only this source.
rm -rf "$WT/build"
( cd "$WT" && nice -n 19 make TOOLCHAIN="$LLVM" LLDDIR="$LLD" USE_CLANG=1 EXTRA_CFLAGS="$EXTRA_CFLAGS" -j2 ) > "$OUT/build.log" 2>&1 \
  || { echo "BUILD FAILED, see $OUT/build.log"; tail -30 "$OUT/build.log"; exit 1; }

cp "$WT/build/m1n1.bin" "$WT/build/m1n1-raw.elf" "$WT/build/m1n1.macho" "$WT/build/m1n1.elf" \
   "$WT/build/build_tag.h" "$WT/build/build_cfg.h" "$OUT/"
( cd "$OUT" && shasum -a 256 m1n1.bin m1n1-raw.elf m1n1.macho m1n1.elf > sha256.txt )

# --- compare against the stable artifact (read-only) ------------------------
GOT_SHA="$(sha "$OUT/m1n1.bin")"; GOT_SIZE="$(stat -f %z "$OUT/m1n1.bin")"
REF_SHA="$(sha "$STABLE")";       REF_SIZE="$(stat -f %z "$STABLE")"
{
  echo "rebuilt : $GOT_SHA $GOT_SIZE $OUT/m1n1.bin"
  echo "stable  : $REF_SHA $REF_SIZE $STABLE"
  echo "expected: $EXPECT_STABLE $EXPECT_STABLE_SIZE (task pin)"
  if [ "$REF_SHA" != "$EXPECT_STABLE" ] || [ "$REF_SIZE" != "$EXPECT_STABLE_SIZE" ]; then
    echo "WARNING: stable artifact on disk does not match the task pin"
  fi
  if [ "$GOT_SHA" = "$REF_SHA" ] && [ "$GOT_SIZE" = "$REF_SIZE" ]; then
    echo "RESULT: IDENTICAL"
  else
    echo "RESULT: DIFFERENT"
    echo "differing bytes (cmp -l | wc -l): $(cmp -l "$OUT/m1n1.bin" "$STABLE" | wc -l | tr -d ' ')"
    echo "first difference: $(cmp "$OUT/m1n1.bin" "$STABLE" 2>&1 | head -1)"
  fi
} > "$OUT/compare.txt"

python3 - "$OUT" "$EXPECT_COMMIT" "$TAG_NOW" "$EXPECT_PATCH" "$EXPECT_FATFS" "$EXTRA_CFLAGS" \
          "$STABLE" "$REF_SHA" "$REF_SIZE" "$GOT_SHA" "$GOT_SIZE" <<'EOF'
import json, sys, hashlib, os
out, commit, tag, patch, fatfs, cflags, stable, ref_sha, ref_size, got_sha, got_size = sys.argv[1:]
def h(p): return hashlib.sha256(open(p,'rb').read()).hexdigest()
m = {
  "session": "S205",
  "purpose": "independent clean rebuild of m1n1-s163-eoi-gap50.bin",
  "worktree": "/Volumes/X31/NWOAS/m1n1-rebuild-s205",
  "base_commit": commit,
  "build_tag": tag,
  "companion_patch": {"path": "nwoas_scripts/publish-s198/companion/m1n1_windows.patch", "sha256": patch},
  "rust_fatfs_commit": fatfs,
  "extra_cflags": cflags,
  "tools": open(os.path.join(out, "tools.txt")).read().splitlines(),
  "outputs": {f: {"sha256": h(os.path.join(out, f)), "bytes": os.path.getsize(os.path.join(out, f))}
              for f in ("m1n1.bin", "m1n1-raw.elf", "m1n1.macho", "m1n1.elf")},
  "stable_reference": {"path": stable, "sha256": ref_sha, "bytes": int(ref_size)},
  "identical": got_sha == ref_sha and got_size == ref_size,
}
json.dump(m, open(os.path.join(out, "manifest.json"), "w"), indent=2)
EOF

echo "built:"; cat "$OUT/build_tag.h"; cat "$OUT/sha256.txt"; cat "$OUT/compare.txt"
