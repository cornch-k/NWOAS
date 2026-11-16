#!/bin/bash
# usage: buildvar.sh <src_tree> <work_dir(new)> <version_tag> [extra make args...]
# Same command as nwoas_scripts/usb-s233/README.md (b); always a fresh copy
# (the Makefile does not track CFLAGS, so never switch NWOAS_AGX inside one build/).
SRC="$1"; WORK="$2"; TAG="$3"; shift 3
if [ -e "$WORK" ]; then echo "work dir exists: $WORK"; exit 2; fi
mkdir -p "$WORK" || exit 2
rsync -a --exclude build "$SRC"/ "$WORK"/ || exit 2
cd "$WORK" || exit 2
rc=0
env RUSTUP_TOOLCHAIN=1.88.0 CARGO_NET_OFFLINE=true CARGO_BUILD_JOBS=2 \
    M1N1_VERSION_TAG="$TAG" \
    RUSTFLAGS="--remap-path-prefix=$HOME/.rustup/toolchains/1.88.0-aarch64-apple-darwin/lib/rustlib/src/rust=/rustc/6b00bc3880198600130e1cf62b8f8a93494488cc" \
    nice -n 19 make TOOLCHAIN=/opt/homebrew/opt/llvm@20/bin/ LLDDIR=/opt/homebrew/opt/lld@20/bin/ USE_CLANG=1 \
    EXTRA_CFLAGS='-DNWOAS_NVME_MAX_BLOCKS=256 -DNWOAS_NVME_REASSERT_US=50 -DNWOAS_FL_RING_FULL_CLEAN=1' "$@" -j4 > build.log 2>&1 || rc=$?
echo "rc=$rc warnings=$(grep -c 'warning:' build.log) errors=$(grep -c 'error:' build.log)"
[ -f build/m1n1.bin ] && { stat -f %z build/m1n1.bin; shasum -a 256 build/m1n1.bin; }
exit $rc
