#!/bin/bash
# S208 harness: extract the real call site verbatim from the candidate source,
# compile it plain and under ASan+UBSan, run both the fixed and pre-fix units.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../hv_exc-s208.c"
GIC=/Volumes/X31/NWOAS/m1n1-diag-s208/src/hv_vgic.h
OUT="$HERE"

[ -f "$SRC" ] || { echo "STOP: $SRC missing"; exit 1; }

# --- locate fragments by content (not fixed line numbers) -----------------
NOTE_START=$(grep -n '^static u64 nwoas_nvme_last_eoi;' "$SRC" | head -1 | cut -d: -f1)
NOTE_END=$(awk 'NR>='"$NOTE_START"' && /^}/ {print NR; exit}' "$SRC")   # end of note_eoi fn
LOOP_START=$(grep -n 'if(misr != 0 && eisr != 0){' "$SRC" | head -1 | cut -d: -f1)
# loop ends when braces opened since LOOP_START return to zero (brace-matched)
LOOP_END=$(awk 'NR>='"$LOOP_START"' {
    n=gsub(/{/,"{"); depth+=n; m=gsub(/}/,"}"); depth-=m;
    if (started && depth==0) { print NR; exit }
    if (n>0) started=1
}' "$SRC")

echo "note_eoi fragment: lines $NOTE_START-$NOTE_END"
echo "loop fragment    : lines $LOOP_START-$LOOP_END"

sed -n "${NOTE_START},${NOTE_END}p" "$SRC" > "$OUT/frag_note_eoi.inc"
sed -n "${LOOP_START},${LOOP_END}p" "$SRC" > "$OUT/frag_loop.inc"

# sanity: the fixed call site must be present verbatim in the loop fragment
grep -q 'nwoas_nvme_note_eoi(intd, hv_get_elr());' "$OUT/frag_loop.inc" \
  || { echo "STOP: fixed call site not found in extracted loop fragment"; exit 1; }

# Build the pre-fix contrast loop fragment by reverting ONLY the call line and
# dropping the S208 comment block (kept minimal, still the real surrounding loop).
python3 - "$OUT/frag_loop.inc" "$OUT/frag_loop_s163.inc" <<'PY'
import sys, re
src = open(sys.argv[1]).read()
# remove the S208 comment block
src = re.sub(r'[ ]*/\* S208:.*?\*/\n', '', src, flags=re.S)
src = src.replace('nwoas_nvme_note_eoi(intd, hv_get_elr());',
                  'nwoas_nvme_note_eoi(intd, ctx->elr);')
open(sys.argv[2], 'w').write(src)
print("wrote pre-fix contrast fragment")
PY
grep -q 'nwoas_nvme_note_eoi(intd, ctx->elr);' "$OUT/frag_loop_s163.inc" \
  || { echo "STOP: pre-fix call site not produced"; exit 1; }

grep -E "^#define ICH_LR_VIRTUAL_(MASK|SHIFT) " "$GIC" > "$OUT/frag_gic_fields.inc"
CC=${CC:-/usr/bin/clang}
WARN="-std=c11 -Wall -Wextra -Werror -Wno-unused-function"

run() { echo "--- $*"; "$@"; }

echo "=== compile + run (plain) ==="
run "$CC" $WARN -O2                    "$OUT/harness.c" -o "$OUT/harness_s208_plain"
run "$CC" $WARN -O2 -DS208_CONTRAST    "$OUT/harness.c" -o "$OUT/harness_s163_plain"
"$OUT/harness_s208_plain"
"$OUT/harness_s163_plain"

echo "=== compile + run (UBSan) ==="
UBSAN="-fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g -O1"
run "$CC" $WARN $UBSAN                 "$OUT/harness.c" -o "$OUT/harness_s208_ubsan"
run "$CC" $WARN $UBSAN -DS208_CONTRAST "$OUT/harness.c" -o "$OUT/harness_s163_ubsan"
"$OUT/harness_s208_ubsan"
"$OUT/harness_s163_ubsan"

echo "=== compile + run (ASan+UBSan, capped) ==="
ASAN="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g -O1"
run "$CC" $WARN $ASAN "$OUT/harness.c" -o "$OUT/harness_s208_asan"
run "$CC" $WARN $ASAN -DS208_CONTRAST "$OUT/harness.c" -o "$OUT/harness_s163_asan"
perl -e 'alarm 30; exec @ARGV' "$OUT/harness_s208_asan"
perl -e 'alarm 30; exec @ARGV' "$OUT/harness_s163_asan"
echo "=== ALL HARNESS RUNS PASSED ==="
