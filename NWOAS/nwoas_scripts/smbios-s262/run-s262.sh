#!/bin/bash
# S262 SMBIOS: restart Windows with the S262 payload through the CURRENT restart wrapper, then read the CPU identity.
#   bash run-s262.sh wifi <current launcher .out> <current LOG.link>   (base: wifi-s259/run-s259.sh)
#   bash run-s262.sh bt   <current launcher .out> <current LOG.link>   (base: bt-s261/run-s261.sh)
#   bash run-s262.sh wifi|bt idle -                                   (Mac mini idle at the D4 proxy)
# The base wrapper is read at run time and only these lines are replaced (each must appear exactly once):
#   the launcher call  -> bash $ROOT/smbios-s262/launch-s262.sh wifi|bt
#   the RES= line      -> result file in smbios-s262/runs/
#   the final status job line -> same job, then grep of the "HVLOG: S262 SMBIOS4" UEFI line, then jobs S1, S2, S3
# Same environment switches as the base wrapper.  Not run by the agent that wrote it.
set -eu
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
MODE=${1:-}
case "$MODE" in
  wifi) BASE=$ROOT/wifi-s259/run-s259.sh; LCALL='bash $ROOT/agxdcp-s259-guest-test.sh'
        RESL='RES=$ROOT/wifi-s259/runs/run-s259-$(date "+%Y%m%d-%H%M%S").txt'
        LAST='runjob $J/W1-wifi-pci-status.cmd' ;;
  bt)   BASE=$ROOT/bt-s261/run-s261.sh; LCALL='bash $ROOT/agxdcp-s261-guest-test.sh'
        RESL='RES=$ROOT/bt-s261/runs/run-s261-$(date "+%Y%m%d-%H%M%S").txt'
        LAST='runjob $J/B0-bt-acpi-status.cmd' ;;
  *) echo "usage: $0 wifi|bt <old .out>|idle <old LOG.link>|-"; exit 1 ;;
esac
shift
mkdir -p $ROOT/smbios-s262/runs
DER=$ROOT/smbios-s262/runs/run-s262-$MODE-$(date '+%Y%m%d-%H%M%S').sh
python3 -I - "$BASE" "$DER" "$MODE" "$LCALL" "$RESL" "$LAST" <<'EOF'
import sys
base, der, mode, lcall, resl, last = sys.argv[1:]
t = open(base).read()
for s in (lcall, resl, last):
    assert t.count(s) == 1, ('base wrapper line changed; update run-s262.sh', s)
t = t.replace(lcall, 'bash $ROOT/smbios-s262/launch-s262.sh ' + mode)
t = t.replace(resl, 'RES=$ROOT/smbios-s262/runs/run-s262-%s-$(date "+%%Y%%m%%d-%%H%%M%%S").txt' % mode)
t = t.replace(last, last + '\nsay "UEFI SMBIOS line:"; grep -a "S262 SMBIOS4" $LOG | head -5 | tee -a $RES'
              '\nrunjob $ROOT/smbios-s262/jobs/S1-cpu-identity.cmd'
              '\nrunjob $ROOT/smbios-s262/jobs/S2-smbios-type4-raw.cmd'
              '\nrunjob $ROOT/smbios-s262/jobs/S3-clock-sources.cmd')
open(der, 'w').write(t)
EOF
bash -n "$DER"
echo "[S262-SMBIOS] base=$(basename "$BASE") sha=$(shasum -a 256 "$BASE" | awk '{print $1}') derived=$DER"
exec bash "$DER" "$@"
