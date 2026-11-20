#!/bin/bash
# NWOAS PMU: boot the CURRENT daily launcher (agxdcp-s268-guest-test.sh) with only the HV swapped g262R -> g262T.
# The daily launcher is NOT edited: its text is read at run time and these two lines must each appear exactly once:
#   HV="…/out-hv2/m1n1-g262R-agx-hv2-nvmeqd.bin"  /  HV_SIZE=2326528 HV_SHA=83add623…
# Everything else (payload, modules, NWOAS_S262_HV / NWOAS_NVME_QD / NWOAS_WIFI / NWOAS_BT …) is used as today.
# NWOAS_S262_HV must be unset or 1 (the g262R branch).  Derived script kept in pmu-cyc/runs/.  Not run by its author.
set -eu
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
BASE=$ROOT/agxdcp-s268-guest-test.sh
OLD_HV='    HV="/Users/angigyeom/Desktop/DEV/NWOAS/port-j274/gpu/hv/out-hv2/m1n1-g262R-agx-hv2-nvmeqd.bin"'
OLD_SZ='    HV_SIZE=2326528 HV_SHA=83add623acbd7658449e84b20d664205ac085ae92e841bf0fab47c5554af89d5'
NEW=$ROOT/pmu-cyc/out/m1n1-g262T-agx-hv2-pmu.bin
NEW_SHA=d5ee4781920a13993d186f56f3f37d8ff2d600a5e40b5e7d34f26239a31660af
[ "${NWOAS_S262_HV:-1}" = 1 ] || { echo 'STOP: NWOAS_S262_HV must be unset or 1 (g262T = g262R + PMU fix)'; exit 1; }
[ "$(shasum -a 256 "$NEW" | awk '{print $1}')" = "$NEW_SHA" ] || { echo "STOP: $NEW sha256 changed"; exit 1; }
mkdir -p $ROOT/pmu-cyc/runs
DER=$ROOT/pmu-cyc/runs/launcher-g262T-$(date '+%Y%m%d-%H%M%S').sh
python3 -I - "$BASE" "$DER" "$OLD_HV" "$OLD_SZ" "$NEW" "$NEW_SHA" <<'PY'
import sys
base, der, ohv, osz, new, sha = sys.argv[1:]
t = open(base).read()
assert t.count(ohv + "\n") == 1 and t.count(osz + "\n") == 1, "launcher HV lines changed; update launch-pmu.sh"
t = t.replace(ohv + "\n", '    HV="%s"   # g262T PMU fix (pmu-cyc/launch-pmu.sh)\n' % new)
t = t.replace(osz + "\n", "    HV_SIZE=2326528 HV_SHA=%s\n" % sha)
open(der, "w").write(t)
PY
bash -n "$DER"
echo "[PMU] base=$(basename "$BASE") sha=$(shasum -a 256 "$BASE" | awk '{print $1}') hv=g262T derived=$DER"
exec bash "$DER"
