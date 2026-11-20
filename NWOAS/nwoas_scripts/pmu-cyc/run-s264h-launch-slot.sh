#!/bin/bash
# NWOAS PMU on the C launcher: run-s264g-launch-slot.sh with only the HV swapped s264G -> s264H (= s264G + PMU fix).
#   bash run-s264h-launch-slot.sh <old .out> <old .link>     (same switches as run-s264g-launch-slot.sh)
# Both standalone-s264 scripts are read at run time; replaced lines (each exactly once): in s264g-launch-watch.sh the
# HV= / HV_SIZE= lines, in the slot the launch-watch call.  Derived scripts kept in pmu-cyc/runs/.
# The C launcher has no job channel: the clock check is by eye (Task Manager > Performance > CPU "Speed",
# Cinebench).  Run only after (나)-default passed on s264G.  Not run by the agent that wrote it.  HARDWARE-UNVERIFIED.
set -eu
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
S=$ROOT/standalone-s264
NEW=$ROOT/pmu-cyc/out/m1n1-s264H-agx-hv2-pmu.bin
NEW_SHA=e5070a2e3268a04cb0571c96e4ea2ee382efdcf154c6699ee291a0c9ff466949
[ "$(shasum -a 256 "$NEW" | awk '{print $1}')" = "$NEW_SHA" ] || { echo "STOP: $NEW sha256 changed"; exit 1; }
mkdir -p $ROOT/pmu-cyc/runs
TS=$(date '+%Y%m%d-%H%M%S'); W=$ROOT/pmu-cyc/runs/s264h-launch-watch-$TS.sh; SL=$ROOT/pmu-cyc/runs/run-s264h-launch-slot-$TS.sh
python3 -I - "$S/s264g-launch-watch.sh" "$W" "$S/run-s264g-launch-slot.sh" "$SL" "$NEW" "$NEW_SHA" <<'PY'
import sys
wb, w, sb, sl, new, sha = sys.argv[1:]
t = open(wb).read()
a = 'HV="$ROOT/nwoas_scripts/standalone-s264/out-s264G/m1n1-s264G-agx-hv2-standalone.bin"\n'
b = 'HV_SIZE=2605056 HV_SHA=d1fea3d73ad7d0d7da562802dbdec88527490dfe5f395d28dbd464a324c43ce6\n'
assert t.count(a) == 1 and t.count(b) == 1, "s264g-launch-watch.sh HV lines changed"
t = t.replace(a, 'HV="%s"   # s264H PMU fix\n' % new).replace(b, "HV_SIZE=2605056 HV_SHA=%s\n" % sha)
open(w, "w").write(t)
u = open(sb).read()
c = "bash $ROOT/standalone-s264/s264g-launch-watch.sh"
assert u.count(c) == 1, "slot launch-watch call changed"
open(sl, "w").write(u.replace(c, "bash " + w))
PY
bash -n "$W"; bash -n "$SL"
echo "[PMU-s264H] watch=$W slot=$SL"
exec bash "$SL" "$@"
