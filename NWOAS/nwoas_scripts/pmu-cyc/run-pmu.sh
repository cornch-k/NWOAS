#!/bin/bash
# NWOAS PMU cycle-counter A/B: restart Windows through the CURRENT daily wrapper (bt-s268/run-s268.sh) and measure.
#   bash run-pmu.sh r <current launcher .out> <current LOG.link>   daily HV g262R (baseline, launcher unchanged)
#   bash run-pmu.sh t <current launcher .out> <current LOG.link>   g262T (launch-pmu.sh: only the HV swapped)
#   bash run-pmu.sh r|t idle -                                    (Mac mini idle at the D4 proxy)
# The base wrapper is read at run time; replaced lines (each exactly once): the launcher call (t only), the RES=
# line, and after the last status job: PMCCFILTR lines of the HV log, then jobs S3 (clock sources + cycle rate,
# smbios-s262) and H1 (NWPWR idle/load/timer, power-s260; needs NWPWR.EXE delivered once by S0b).
# Not run by the agent that wrote it.  HARDWARE-UNVERIFIED.
set -eu
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
HV=${1:-}; case "$HV" in r|t) ;; *) echo "usage: $0 r|t <old .out>|idle <old LOG.link>|-"; exit 1;; esac
shift
BASE=$ROOT/bt-s268/run-s268.sh
mkdir -p $ROOT/pmu-cyc/runs
DER=$ROOT/pmu-cyc/runs/run-pmu-$HV-$(date '+%Y%m%d-%H%M%S').sh
python3 -I - "$BASE" "$DER" "$HV" <<'PY'
import sys
base, der, hv = sys.argv[1:]
t = open(base).read()
lcall = 'bash $ROOT/agxdcp-s268-guest-test.sh'
resl = 'RES=$ROOT/bt-s268/runs/run-s268-$(date "+%Y%m%d-%H%M%S").txt'
last = 'runjob $JW/W1-wifi-pci-status.cmd'
for s in (lcall, resl, last):
    assert t.count(s) == 1, ("base wrapper line changed; update run-pmu.sh", s)
if hv == "t":
    t = t.replace(lcall, "bash $ROOT/pmu-cyc/launch-pmu.sh")
t = t.replace(resl, 'RES=$ROOT/pmu-cyc/runs/run-pmu-%s-$(date "+%%Y%%m%%d-%%H%%M%%S").txt' % hv)
t = t.replace(last, last +
    '\nsay "HV PMCCFILTR lines:"; cat $LOG $LOG.vuart 2>/dev/null | grep -a "PMCCFILTR_EL0" | sort | uniq -c | head -8 | tee -a $RES'
    '\nrunjob $ROOT/smbios-s262/jobs/S3-clock-sources.cmd'
    '\nrunjob $ROOT/power-s260/jobs/H1-idle-load.cmd'
    '\nsay "PMU SUMMARY HV=%s: $(grep -a \'^CYC \' $RES | tr \'\\n\' \' \')"' % hv)
open(der, "w").write(t)
PY
bash -n "$DER"
echo "[PMU] base=$(basename "$BASE") sha=$(shasum -a 256 "$BASE" | awk '{print $1}') HV=$HV derived=$DER"
exec bash "$DER" "$@"
