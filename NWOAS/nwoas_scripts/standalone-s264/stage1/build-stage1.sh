#!/bin/bash
# NWOAS 4-1 first-stage m1n1 (PLAN-4-1-disk-boot.md 2장 P1, 2026-11-20 review version)
#   source = the reproduced s264G tree + stage1/hv2-s264G-to-stage1.patch
#            (chainload.c: preoslog + ADT BootArgs moved like proxyclient tools/chainload.py, malloc check;
#             exception.c/utils.c/main.c/payload.c/rust lib.rs: with CHAINLOADING every unhandled exception,
#             SError, Rust panic, panic() and failed assert until the hand-over continues in the USB proxy
#             instead of rebooting; nwoas_stage1_fault=1 = rehearsal of that path;
#             hv_vm.c: DART page pool 1 instead of 48 pages under CHAINLOADING so the image fits 2 MiB)
#   config.h USE_FB off (never touch the display), CHAINLOADING=1, no AGX, RELEASE not set
#   (=> no EARLY_PROXY_TIMEOUT: stage 1 always chainloads; a failure is the only way into its proxy).
# Every output file is padded with zero bytes to EXACTLY the size of D4 (2,097,152 B), so iBoot is
# handed an object of the same size as the one it boots today (PLAN-4-1 2-1).
# Variants (same raw image, different m1n1 variables; only stage1.bin is ever registered):
#   stage1.bin          chainload=<ESP>;m1n1/boot.bin                      (1TR registration + rehearsal)
#   stage1-missing.bin  chainload=<ESP>;m1n1/nwoas-absent.bin              (negative rehearsal: file missing)
#   stage1-fault.bin    nwoas_stage1_fault=1 + chainload=<ESP>;m1n1/boot.bin (negative rehearsal: exception)
#   stage1-42.bin       chainload=<ESP>;m1n1/nwoas-s264g.bin               (4-2 rehearsal from D4)
# Path separator: '/' (host test s264g_auto_test.py: '\' is not found by the m1n1 FAT reader).
# usage: build-stage1.sh <work dir of build-s264g.sh> <new out dir>     (builds only; installs nothing)
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
GW="${1:?usage: build-stage1.sh <build-s264g work dir> <new out dir>}"
OUT="${2:?usage}"
[ -e "$OUT" ] && { echo "out dir exists: $OUT"; exit 2; }
G_SHA=d1fea3d73ad7d0d7da562802dbdec88527490dfe5f395d28dbd464a324c43ce6
PATCH_SHA="${ST1_PATCH_SHA:-9588a8f1d2b1d5ea3c918691be0e58734e94122d5ba27759b5b4c346aac889b4}"
ST1_RAW_SHA="${ST1_RAW_SHA:-cc2809ce111880b99204d54efdfb2d7e4c26120abaf46f62ff80607d83818a5c}"
D4_SIZE=2097152
ESP=a3e73003-c14a-487b-9f2e-8854fe119192
[ "$(shasum -a 256 "$GW/build-s264G/build/m1n1.bin" | awk '{print $1}')" = "$G_SHA" ] \
    || { echo "STOP: $GW is not a reproduced s264G work dir"; exit 2; }
[ "$(shasum -a 256 "$HERE/hv2-s264G-to-stage1.patch" | awk '{print $1}')" = "$PATCH_SHA" ] \
    || { echo "STOP: hv2-s264G-to-stage1.patch differs from the recorded one"; exit 2; }
mkdir -p "$OUT"
rsync -a --exclude build "$GW/src-s264G/" "$OUT/src/"
( cd "$OUT/src" && patch -p1 -s --no-backup-if-mismatch < "$HERE/hv2-s264G-to-stage1.patch" )
sed -i '' 's|^#define USE_FB$|// #define USE_FB  /* NWOAS 4-1 stage 1: never touch the display (D4 / the HV does it) */|' "$OUT/src/config.h"
! grep -q '^#define USE_FB' "$OUT/src/config.h" || { echo "STOP: USE_FB still on"; exit 2; }
! grep -q '^#define EARLY_PROXY_TIMEOUT' "$OUT/src/config.h" || { echo "STOP: EARLY_PROXY_TIMEOUT on"; exit 2; }
bash "$ROOT/port-j274/gpu/hv/tools-hv2/buildvar.sh" "$OUT/src" "$OUT/build" stage1-s264G CHAINLOADING=1
RAW="$OUT/build/build/m1n1.bin"
PS=$(/opt/homebrew/opt/llvm@20/bin/llvm-nm "$OUT/build/build/m1n1-raw.elf" | awk '$3=="_payload_start"{print $1}')
[ "$((16#$PS))" = "$(stat -f %z "$RAW")" ] || { echo "STOP: _payload_start 0x$PS != raw size"; exit 1; }
grep -aq 'NWOAS-STAGE1: USB proxy fallback' "$RAW" || { echo "STOP: no fallback code"; exit 1; }
grep -aq 'Searching for partition UUID' "$RAW" || { echo "STOP: no chainload code"; exit 1; }
python3 -I - "$RAW" "$OUT" "$D4_SIZE" "$ESP" <<'PY'
import sys, os
raw = open(sys.argv[1], "rb").read()
out, size, esp = sys.argv[2], int(sys.argv[3]), sys.argv[4]
variants = {
    "stage1.bin":         [f"chainload={esp};m1n1/boot.bin"],
    "stage1-missing.bin": [f"chainload={esp};m1n1/nwoas-absent.bin"],
    "stage1-fault.bin":   ["nwoas_stage1_fault=1", f"chainload={esp};m1n1/boot.bin"],
    "stage1-42.bin":      [f"chainload={esp};m1n1/nwoas-s264g.bin"],
}
for name, v in variants.items():
    data = raw + b"".join(i.encode() + b"\n" for i in v) + b"\0\0\0\0"
    assert len(data) <= size, (name, len(data))
    data += b"\0" * (size - len(data))
    open(os.path.join(out, name), "wb").write(data)
PY
echo "raw    $(stat -f %z "$RAW") B (_payload_start 0x$PS) sha256 $(shasum -a 256 "$RAW" | awk '{print $1}')"
for f in stage1.bin stage1-missing.bin stage1-fault.bin stage1-42.bin; do
    [ "$(stat -f %z "$OUT/$f")" = "$D4_SIZE" ] || { echo "STOP: $f is not $D4_SIZE B"; exit 1; }
    echo "$f $(stat -f %z "$OUT/$f") B sha256 $(shasum -a 256 "$OUT/$f" | awk '{print $1}') md5 $(md5 -q "$OUT/$f")"
done
[ "$(shasum -a 256 "$RAW" | awk '{print $1}')" = "$ST1_RAW_SHA" ] && echo "stage1 reproduced: raw $ST1_RAW_SHA" \
    || { echo "stage1 raw differs from the recorded one"; exit 1; }
( cd "$OUT" && shasum -a 256 stage1.bin stage1-missing.bin stage1-fault.bin stage1-42.bin ) > "$OUT/stage1.sha256"
diff "$OUT/stage1.sha256" "$HERE/stage1-variants.sha256" || { echo "STOP: variant hashes differ"; exit 1; }
