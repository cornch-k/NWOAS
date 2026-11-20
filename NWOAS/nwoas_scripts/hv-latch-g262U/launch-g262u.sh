#!/bin/bash
# NWOAS G262U: boot the CURRENT daily launcher (agxdcp-s268-guest-test.sh) with only the HV swapped g262R -> g262U (g262T PMU fix + scanout latch fix).
# The daily launcher is NOT edited: its text is read at run time and these two lines must each appear exactly once:
#   HV="…/out-hv2/m1n1-g262R-agx-hv2-nvmeqd.bin"  /  HV_SIZE=2326528 HV_SHA=83add623…
# Everything else (payload, modules, NWOAS_S262_HV / NWOAS_NVME_QD / NWOAS_WIFI / NWOAS_BT …) is used as today.
# NWOAS_S262_HV must be unset or 1 (the g262R branch).  Derived script kept in hv-latch-g262U/runs/.  Not run by its author.
set -eu
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
BASE=$ROOT/agxdcp-s268-guest-test.sh
OLD_HV='    HV="/Users/angigyeom/Desktop/DEV/NWOAS/port-j274/gpu/hv/out-hv2/m1n1-g262R-agx-hv2-nvmeqd.bin"'
OLD_SZ='    HV_SIZE=2326528 HV_SHA=83add623acbd7658449e84b20d664205ac085ae92e841bf0fab47c5554af89d5'
NEW=$ROOT/hv-latch-g262U/out/m1n1-g262U-agx-hv2-latch.bin
NEW_SHA=7ef111d7e0bcc1b2b769818354e860afd26e8773f233e9757f9461a529ce9d7a
[ "${NWOAS_S262_HV:-1}" = 1 ] || { echo 'STOP: NWOAS_S262_HV must be unset or 1 (g262U = g262T + latch fix)'; exit 1; }
[ "$(shasum -a 256 "$NEW" | awk '{print $1}')" = "$NEW_SHA" ] || { echo "STOP: $NEW sha256 changed"; exit 1; }
mkdir -p $ROOT/hv-latch-g262U/runs
DER=$ROOT/hv-latch-g262U/runs/launcher-g262U-$(date '+%Y%m%d-%H%M%S').sh
python3 -I - "$BASE" "$DER" "$OLD_HV" "$OLD_SZ" "$NEW" "$NEW_SHA" <<'PY'
import sys
base, der, ohv, osz, new, sha = sys.argv[1:]
t = open(base).read()
assert t.count(ohv + "\n") == 1 and t.count(osz + "\n") == 1, "launcher HV lines changed; update launch-pmu.sh"
t = t.replace(ohv + "\n", '    HV="%s"   # g262U latch fix + PMU (hv-latch-g262U/launch-g262u.sh)\n' % new)
t = t.replace(osz + "\n", "    HV_SIZE=2326528 HV_SHA=%s\n" % sha)
open(der, "w").write(t)
PY
bash -n "$DER"
echo "[G262U] base=$(basename "$BASE") sha=$(shasum -a 256 "$BASE" | awk '{print $1}') hv=g262U derived=$DER"
exec bash "$DER"
