#!/bin/bash
# NWOAS Wi-Fi: firmware-only repackage of an existing driver package (no compile, nwoa4378.sys kept byte for byte).
#   bash recat-fw.sh <base package dir> <firmware dir> <DriverVer version a.b.c.d> <stamp suffix>
# <firmware dir> holds the five files the INF ships: atlantisb-X3.{trx,clmb,txcb}, P-atlantisb_M-RASP_V-{m,u}.txt.
# The board file (nwoas-bcm4378-board.bin, ADT cal + MAC) comes from the base package.
# Only the DriverVer version is changed in the INF (date kept), so the new package ranks above the base package.
# Catalog regenerated + signed in the Parallels VM by build/recat-wifi-fw.ps1 (same test certificate).
# Output: ./out/<date>-<time>-<suffix>/{package,logs,SHA256SUMS.txt}.  Nothing here touches the Mac mini.
# The firmware files are Apple's: they only go into the private package, never into the repo.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
BASE=${1:?base package dir}; FW=${2:?firmware dir}; VER=${3:?version}; SUF=${4:?suffix}
SHARE=/Volumes/media/nwoas/wifi-build
VM="Windows 11"
STAMP=$(date +%Y%m%d-%H%M)-$SUF
IN=$SHARE/recat/$STAMP
say() { echo "$(date +%T) $*"; }
[ -d /Volumes/media/nwoas ] || { echo "STOP /Volumes/media/nwoas is not mounted"; exit 1; }
echo "$VER" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$' || { echo "STOP bad version $VER"; exit 1; }
[ -e "$HERE/out/$STAMP" ] && { echo "STOP $HERE/out/$STAMP exists"; exit 1; }
mkdir -p "$IN" "$SHARE/scripts" || exit 1

say "inputs -> $IN"
cp "$BASE/nwoa4378.sys" "$BASE/nwoas-bcm4378-board.bin" "$IN/" || { echo "STOP copy base"; exit 1; }
for f in atlantisb-X3.trx atlantisb-X3.clmb atlantisb-X3.txcb P-atlantisb_M-RASP_V-m.txt P-atlantisb_M-RASP_V-u.txt; do
  cp "$FW/$f" "$IN/$f" || { echo "STOP missing firmware $f"; exit 1; }
done
# DriverVer = <date>,<version>: keep the date, change the version only (CRLF kept)
OLD=$(tr -d '\r' < "$BASE/nwoa4378.inf" | sed -n 's/^DriverVer = \([0-9/]*\),\(.*\)$/\1 \2/p')
[ -n "$OLD" ] || { echo "STOP no DriverVer line in base INF"; exit 1; }
DATE=${OLD% *}
CR=$'\r'
LC_ALL=C sed "s#^DriverVer = .*\$#DriverVer = $DATE,$VER$CR#" "$BASE/nwoa4378.inf" > "$IN/nwoa4378.inf" || exit 1
n=$(LC_ALL=C cmp -l "$BASE/nwoa4378.inf" "$IN/nwoa4378.inf" 2>/dev/null | wc -l | tr -d ' ')
say "INF DriverVer $OLD -> $DATE $VER (bytes differing: $n)"
diff <(tr -d '\r' < "$BASE/nwoa4378.inf") <(tr -d '\r' < "$IN/nwoa4378.inf")
( cd "$IN" && shasum -a 256 * | sed 's/^/in /' )
cp "$HERE/build/recat-wifi-fw.ps1" "$SHARE/scripts/recat-wifi-fw.ps1" || { echo "STOP copy ps1"; exit 1; }

st=$(prlctl list -a 2>/dev/null | awk -v n="$VM" '{ s=$2; $1=$2=$3=""; sub(/^ +/,""); if ($0==n) print s }')
say "VM state: ${st:-unknown}"
case "$st" in
  suspended|paused) prlctl resume "$VM" >/dev/null || { echo "STOP resume"; exit 1; }; sleep 5 ;;
  stopped) prlctl start "$VM" >/dev/null || { echo "STOP start"; exit 1; }; sleep 60 ;;
esac
say "VM recat stamp=$STAMP"
prlctl exec "$VM" powershell -NoProfile -ExecutionPolicy Bypass -File '\\Mac\media\nwoas\wifi-build\scripts\recat-wifi-fw.ps1' -Stamp "$STAMP" \
  > "$SHARE/logs/vm-recat-$STAMP.txt" 2>&1
rc=$?
head -c 20000 "$SHARE/logs/vm-recat-$STAMP.txt" | tr -d '\r' | tail -40
rm -rf "$IN"
[ $rc = 0 ] || { echo "STOP VM recat rc=$rc (log $SHARE/logs/vm-recat-$STAMP.txt)"; exit 2; }
mkdir -p "$HERE/out"
rsync -a "$SHARE/out/$STAMP/" "$HERE/out/$STAMP/" || { echo "STOP copy back"; exit 1; }
cmp -s "$BASE/nwoa4378.sys" "$HERE/out/$STAMP/package/nwoa4378.sys" && say "nwoa4378.sys identical to base" || { echo "STOP sys differs from base"; exit 3; }
say "package: $HERE/out/$STAMP/package"
( cd "$HERE/out/$STAMP/package" && shasum -a 256 * )
echo "RECAT DONE stamp=$STAMP"
