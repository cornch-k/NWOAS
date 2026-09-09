#!/bin/bash
# S199: fixture tests for verify_vendor_tree.sh. Everything happens in a
# mktemp directory; the real worktree, the m1n1_windows submodule store and
# the S197 outputs are only read.
#
# Part A copies the S197 vendored rust-fatfs tree and mutates the copy in ways
# that keep the file list (and therefore the old `find | diff` guard) unchanged.
# Part B builds a throwaway git repo with a symlink entry, because the pinned
# rust-fatfs tree has no symlinks, so symlink handling would otherwise be
# untested.
set -u
HERE=$(cd "$(dirname "$0")" && pwd -P)
V="$HERE/verify_vendor_tree.sh"
ROOT=/Volumes/X31/NWOAS
SRC="$ROOT/m1n1-guest-s197/rust/vendor/rust-fatfs"
REPO="$ROOT/m1n1_windows/rust/vendor/rust-fatfs"
PIN=4eccb50d011146fbed20e133d33b22f3c27292e7

T=$(mktemp -d "${TMPDIR:-/tmp}/vvt-test.XXXXXX") || exit 2
trap 'rm -rf "$T"' EXIT
pass=0; failc=0
expect() { # expect <0|1> <name> <cmd...>
  want=$1; name=$2; shift 2
  out=$("$@" 2>&1); rc=$?
  if [ "$rc" = "$want" ]; then pass=$((pass + 1)); echo "ok   [$name] rc=$rc"
  else failc=$((failc + 1)); echo "FAIL [$name] rc=$rc want=$want"; echo "$out" | sed 's/^/     /'; fi
}
fresh() { rm -rf "$T/fx"; cp -Rp "$SRC" "$T/fx"; }

# ---------------------------------------------------------------- Part A
fresh
expect 0 "A0 unmodified copy passes"            "$V" "$T/fx" "$REPO" "$PIN" -q

fresh; f="$T/fx/src/lib.rs"
# same filename, same size, same mtime: flip one byte
printf 'X' | dd of="$f" bs=1 seek=100 conv=notrunc 2>/dev/null; touch -r "$SRC/src/lib.rs" "$f"
expect 1 "A1 one byte changed, same name/size/mtime" "$V" "$T/fx" "$REPO" "$PIN" -q

fresh; printf '\n' >> "$T/fx/Cargo.toml"
expect 1 "A2 trailing newline appended"          "$V" "$T/fx" "$REPO" "$PIN" -q

fresh; chmod +x "$T/fx/Cargo.toml"
expect 1 "A3 exec bit added to 100644 file"      "$V" "$T/fx" "$REPO" "$PIN" -q

fresh; chmod -x "$T/fx/build-nostd.sh"
expect 1 "A4 exec bit removed from 100755 file"  "$V" "$T/fx" "$REPO" "$PIN" -q

fresh; cp "$T/fx/src/lib.rs" "$T/lib.rs.copy"; rm "$T/fx/src/lib.rs"; ln -s "$T/lib.rs.copy" "$T/fx/src/lib.rs"
expect 1 "A5 file replaced by symlink to identical content" "$V" "$T/fx" "$REPO" "$PIN" -q

fresh; echo x > "$T/fx/src/extra.rs"
expect 1 "A6 extra file"                         "$V" "$T/fx" "$REPO" "$PIN" -q

fresh; rm "$T/fx/src/lib.rs"
expect 1 "A7 missing file"                       "$V" "$T/fx" "$REPO" "$PIN" -q

fresh; mv "$T/fx/src" "$T/src.moved"; ln -s "$T/src.moved" "$T/fx/src"
expect 1 "A8 directory replaced by symlink"      "$V" "$T/fx" "$REPO" "$PIN" -q

fresh; mkdir -p "$T/fx/target/debug"; echo o > "$T/fx/target/debug/build.o"
expect 1 "A9 stray build output dir"             "$V" "$T/fx" "$REPO" "$PIN" -q

fresh; echo gitdir: /nowhere > "$T/fx/.git"
expect 0 "A10 top-level .git gitfile is ignored"  "$V" "$T/fx" "$REPO" "$PIN" -q

fresh; expect 2 "A11 unknown tree-ish is a usage error" "$V" "$T/fx" "$REPO" 0000000000000000000000000000000000000000 -q

# read-only check: the real worktree tree and the S197 out/ must be untouched
expect 0 "A12 real S197 vendored tree still passes" "$V" "$SRC" "$REPO" "$PIN" -q

# ---------------------------------------------------------------- Part B
R="$T/repo"; mkdir -p "$R"
( cd "$R" && git init -q && git config user.email t@t && git config user.name t \
  && git config core.symlinks true \
  && mkdir sub && printf 'hello\n' > sub/a.txt && printf '#!/bin/sh\n' > run.sh && chmod +x run.sh \
  && ln -s sub/a.txt link && git add -A && git commit -q -m fixture ) || { echo "fixture repo failed"; exit 2; }
BC=$(git -C "$R" rev-parse HEAD)
[ "$(git -C "$R" ls-tree HEAD link | cut -c1-6)" = 120000 ] || { echo "fixture: symlink not recorded as 120000"; exit 2; }
freshb() { rm -rf "$T/fb"; mkdir "$T/fb"; git -C "$R" archive "$BC" | tar -x -C "$T/fb"; }

freshb
expect 0 "B0 archived fixture with symlink passes" "$V" "$T/fb" "$R" "$BC" -q

freshb; rm "$T/fb/link"; ln -s sub/b.txt "$T/fb/link"
expect 1 "B1 symlink target changed"             "$V" "$T/fb" "$R" "$BC" -q

freshb; rm "$T/fb/link"; printf 'hello\n' > "$T/fb/link"
expect 1 "B2 symlink replaced by regular file"    "$V" "$T/fb" "$R" "$BC" -q

freshb; chmod -x "$T/fb/run.sh"
expect 1 "B3 exec bit removed in fixture"         "$V" "$T/fb" "$R" "$BC" -q

echo "----"
echo "passed $pass, failed $failc"
[ "$failc" = 0 ]
