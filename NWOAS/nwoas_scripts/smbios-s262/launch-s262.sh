#!/bin/bash
# S262 SMBIOS (test payload): boot the CURRENT launcher with only the UEFI payload swapped.
#   bash launch-s262.sh wifi   = agxdcp-s259-guest-test.sh (Wi-Fi + sound + GPU) with m1n1-payload-s262-wifi.bin
#   bash launch-s262.sh bt     = agxdcp-s261-guest-test.sh (+ Bluetooth)          with m1n1-payload-s262-bt.bin
# The original launcher is NOT edited or copied by hand: its text is read at run time, the two lines
#   PAYLOAD="<S259 or S261 payload>"  /  PAYLOAD_SHA=<its sha>
# must each appear exactly once and are replaced; anything else in the launcher (HV, modules, env switches such
# as NWOAS_S262_HV / NWOAS_S260_HV / NWOAS_WIFI / NWOAS_BT / NWOAS_DELIVER_MANIFEST) is used as it is today.
# NWOAS_S260_MADT=1 is refused (that switch selects the S260 payload, which has no S262 SMBIOS change).
# The derived script is kept in smbios-s262/runs/ as a record.  Not run by the agent that wrote it.
set -eu
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
OUTD=$ROOT/fw-smbios-s262/out
case "${1:-}" in
  wifi) BASE=$ROOT/agxdcp-s259-guest-test.sh
        OLD_P=$ROOT/fw-wifi-s259/out/s259/m1n1-payload-s259-wifi.bin
        OLD_S=fbc4406b0103cf028be5be2eb8ec60844af8952d9c845405338d48cb64e50497
        NEW_P=$OUTD/s262-wifi/m1n1-payload-s262-wifi.bin
        NEW_S=b1364aff375227462cddfea994d32a4aa4d5a4ea63d48f3a2ece482cd3c46b00 ;;
  bt)   BASE=$ROOT/agxdcp-s261-guest-test.sh
        OLD_P=$ROOT/fw-bt-s261/out/s261/m1n1-payload-s261-bt.bin
        OLD_S=26428b579ba117ae3b26c8d914461cc125b07140de1c1bf3393ff1a62fd0ab92
        NEW_P=$OUTD/s262-bt/m1n1-payload-s262-bt.bin
        NEW_S=7d79b27a1c7ea1d9d5e4320828239125bf7387a615d0a6effef1c3109e38b8a5 ;;
  *) echo "usage: $0 wifi|bt"; exit 1 ;;
esac
[ "${NWOAS_S260_MADT:-0}" = 1 ] && { echo 'STOP: NWOAS_S260_MADT=1 selects the S260 payload (no S262 SMBIOS change)'; exit 1; }
[ "$(stat -f %z "$NEW_P")" -eq 33669120 ]
[ "$(shasum -a 256 "$NEW_P" | awk '{print $1}')" = "$NEW_S" ] || { echo "STOP: $NEW_P sha256 changed"; exit 1; }
mkdir -p $ROOT/smbios-s262/runs
DER=$ROOT/smbios-s262/runs/launcher-s262-$1-$(date '+%Y%m%d-%H%M%S').sh
python3 -I - "$BASE" "$DER" "$OLD_P" "$OLD_S" "$NEW_P" "$NEW_S" <<'EOF'
import sys
base, der, op, os_, np_, ns = sys.argv[1:]
t = open(base).read()
a, b = 'PAYLOAD="%s"\n' % op, 'PAYLOAD_SHA=%s\n' % os_
assert t.count(a) == 1 and t.count(b) == 1, 'launcher payload lines changed; update launch-s262.sh'
t = t.replace(a, 'PAYLOAD="%s"   # S262 SMBIOS (launch-s262.sh)\n' % np_).replace(b, 'PAYLOAD_SHA=%s\n' % ns)
open(der, 'w').write(t)
EOF
echo "[S262-SMBIOS] base=$(basename "$BASE") sha=$(shasum -a 256 "$BASE" | awk '{print $1}') payload=$(basename "$NEW_P") derived=$DER"
exec bash "$DER"
