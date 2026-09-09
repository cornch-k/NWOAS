#!/bin/bash
# S199: read-only, per-file verification that an on-disk directory is
# byte-identical to a pinned git tree.
#
#   verify_vendor_tree.sh <dir> <git-repo> <tree-ish> [-q]
#
#   <dir>       directory to verify (e.g. a `git archive`d vendored tree)
#   <git-repo>  any git repository (or submodule store) that contains <tree-ish>
#   <tree-ish>  commit or tree id to compare against
#   -q          print only the verdict and failures
#
# What is checked, for every entry of `git ls-tree -r <tree-ish>`:
#   100644 / 100755  on-disk path must be a regular file (not a symlink); its
#                    raw bytes are hashed with `git hash-object --no-filters`
#                    (no -w, nothing is written) and must equal the tree blob id;
#                    the executable bit must match the tree mode.
#   120000           on-disk path must be a symlink whose link target string
#                    hashes to the tree blob id.
#   160000 (gitlink) not verifiable here -> FAIL (nested submodule).
#   anything else    FAIL.
# In addition:
#   - every directory component of a tree path must be a real directory, not a
#     symlink (otherwise a symlinked directory could alias the content);
#   - every file or symlink present on disk that is not in the tree is reported
#     as EXTRA and fails, except a top-level `.git` gitfile/dir, which a real
#     submodule checkout carries and which is not a build input.
#
# The script never writes to <dir> or <git-repo>. Exit 0 only if everything
# matches; exit 1 on any mismatch; exit 2 on usage/environment error.
set -u

usage() { echo "usage: $0 <dir> <git-repo> <tree-ish> [-q]" >&2; exit 2; }
[ $# -ge 3 ] || usage
DIR=$1; REPO=$2; TREE=$3; QUIET=0
[ "${4:-}" = "-q" ] && QUIET=1

[ -d "$DIR" ] || { echo "FAIL: not a directory: $DIR" >&2; exit 2; }
git -C "$REPO" rev-parse --is-inside-work-tree >/dev/null 2>&1 \
  || git -C "$REPO" rev-parse --git-dir >/dev/null 2>&1 \
  || { echo "FAIL: not a git repository: $REPO" >&2; exit 2; }
TREE_ID=$(git -C "$REPO" rev-parse --verify "${TREE}^{tree}" 2>/dev/null) \
  || { echo "FAIL: $TREE is not a tree/commit in $REPO" >&2; exit 2; }
export GIT_OPTIONAL_LOCKS=0

DIR=$(cd "$DIR" && pwd -P)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/vvt.XXXXXX") || exit 2
trap 'rm -rf "$TMP"' EXIT

fails=0; n_blob=0; n_link=0
fail() { fails=$((fails + 1)); echo "MISMATCH: $*"; }
note() { [ $QUIET = 1 ] || echo "$*"; }

# --- 1. every tree entry must exist on disk with matching type, mode, bytes ---
git -C "$REPO" ls-tree -r -z "$TREE_ID" > "$TMP/tree.z" || exit 2
: > "$TMP/tree_paths"
: > "$TMP/tree_dirs"
while IFS= read -r -d '' entry; do
  meta=${entry%%$'\t'*}; path=${entry#*$'\t'}
  mode=${meta%% *}; rest=${meta#* }; type=${rest%% *}; rest=${rest#* }; blob=${rest%% *}
  printf '%s\n' "$path" >> "$TMP/tree_paths"
  d=$path
  while [ "${d%/*}" != "$d" ]; do d=${d%/*}; printf '%s\n' "$d" >> "$TMP/tree_dirs"; done
  f="$DIR/$path"
  case "$mode:$type" in
    100644:blob|100755:blob)
      n_blob=$((n_blob + 1))
      if [ -L "$f" ]; then fail "$path: is a symlink, tree has a regular file ($mode)"; continue; fi
      if [ ! -f "$f" ]; then fail "$path: missing on disk"; continue; fi
      got=$(git -C "$REPO" hash-object --no-filters -- "$f") || { fail "$path: hash-object failed"; continue; }
      [ "$got" = "$blob" ] || fail "$path: content sha1 $got != tree blob $blob"
      if [ "$mode" = 100755 ]; then
        [ -x "$f" ] || fail "$path: tree mode 100755 but file is not executable"
      else
        [ ! -x "$f" ] || fail "$path: tree mode 100644 but file is executable"
      fi ;;
    120000:blob)
      n_link=$((n_link + 1))
      if [ ! -L "$f" ]; then fail "$path: tree has a symlink, on disk is not a symlink"; continue; fi
      target=$(readlink "$f")
      got=$(printf '%s' "$target" | git -C "$REPO" hash-object --no-filters --stdin) || { fail "$path: hash-object failed"; continue; }
      [ "$got" = "$blob" ] || fail "$path: symlink target '$target' hashes to $got != tree blob $blob" ;;
    160000:commit)
      fail "$path: nested submodule (gitlink $blob) cannot be verified by this script" ;;
    *)
      fail "$path: unsupported tree entry mode=$mode type=$type" ;;
  esac
done < "$TMP/tree.z"

# --- 2. directory components must be real directories ------------------------
sort -u "$TMP/tree_dirs" | while IFS= read -r d; do
  if [ -L "$DIR/$d" ]; then echo "MISMATCH: $d/: is a symlink, tree has a directory"; fi
  if [ ! -d "$DIR/$d" ]; then echo "MISMATCH: $d/: directory missing"; fi
done > "$TMP/dir_fail"
if [ -s "$TMP/dir_fail" ]; then cat "$TMP/dir_fail"; fails=$((fails + $(wc -l < "$TMP/dir_fail"))); fi

# --- 3. nothing on disk that is not in the tree -------------------------------
( cd "$DIR" && find . \( -type f -o -type l \) -print | sed 's|^\./||' | grep -v '^\.git$' | grep -v '^\.git/' ) | sort > "$TMP/disk_paths"
sort "$TMP/tree_paths" > "$TMP/tree_sorted"
comm -23 "$TMP/disk_paths" "$TMP/tree_sorted" > "$TMP/extra"
if [ -s "$TMP/extra" ]; then
  while IFS= read -r p; do fail "$p: EXTRA, not in tree"; done < "$TMP/extra"
fi

n_tree=$(wc -l < "$TMP/tree_paths" | tr -d ' ')
if [ "$fails" = 0 ]; then
  note "OK: $DIR matches $TREE (tree $TREE_ID): $n_tree entries, $n_blob files, $n_link symlinks, no extras"
  exit 0
else
  echo "FAIL: $DIR differs from tree $TREE_ID ($fails mismatch(es))"
  exit 1
fi
