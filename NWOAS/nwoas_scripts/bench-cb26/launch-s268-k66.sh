#!/bin/bash
# K66 Cinebench-corruption repro launcher: the CURRENT daily launcher agxdcp-s268-guest-test.sh, read at run time.
#   CB_PAYLOAD=s261 -> only the two payload lines (today: S280 BT + MADT) are swapped back to the S261 BT payload
#                      (fw-bt-s261/out/s261/m1n1-payload-s261-bt.bin), everything else as daily (HV g262U, QD 32, BT)
#   otherwise       -> the daily launcher runs unchanged.
# Each replaced line must appear exactly once.  The daily launcher is NOT edited; the derived copy is kept in
# bench-cb26/runs/.  Not run by its author.
set -eu
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
BASE=$ROOT/agxdcp-s268-guest-test.sh
[ "${CB_PAYLOAD:-}" = s261 ] || exec bash "$BASE"
OLD_P='PAYLOAD="/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts/fw-btmadt-s280/out/s280/m1n1-payload-s280-btmadt.bin"'
OLD_S='PAYLOAD_SHA=8b0730fd6787306f4ada28bf858c5c60c846fe47b4777ccc05d318b5a84da158'
NEW_P=$ROOT/fw-bt-s261/out/s261/m1n1-payload-s261-bt.bin
NEW_S=26428b579ba117ae3b26c8d914461cc125b07140de1c1bf3393ff1a62fd0ab92
[ "$(stat -f %z "$NEW_P")" -eq 33669120 ] && [ "$(shasum -a 256 "$NEW_P" | awk '{print $1}')" = "$NEW_S" ] \
    || { echo "STOP: $NEW_P is not the recorded S261 payload"; exit 1; }
mkdir -p $ROOT/bench-cb26/runs
DER=$ROOT/bench-cb26/runs/launcher-s268-s261-$(date '+%Y%m%d-%H%M%S').sh
python3 -I - "$BASE" "$DER" "$OLD_P" "$OLD_S" "$NEW_P" "$NEW_S" <<'PY'
import sys
base, der, op, os_, np_, ns = sys.argv[1:]
t = open(base).read()
assert t.count(op + "\n") == 1 and t.count(os_ + "\n") == 1, "launcher payload lines changed; update launch-s268-k66.sh"
t = t.replace(op + "\n", 'PAYLOAD="%s"   # S261 BT payload (bench-cb26/launch-s268-k66.sh)\n' % np_)
t = t.replace(os_ + "\n", "PAYLOAD_SHA=%s\n" % ns)
open(der, "w").write(t)
PY
bash -n "$DER"
echo "[CB-K66] base=$(basename "$BASE") sha=$(shasum -a 256 "$BASE" | awk '{print $1}') payload=S261 derived=$DER"
exec bash "$DER"
