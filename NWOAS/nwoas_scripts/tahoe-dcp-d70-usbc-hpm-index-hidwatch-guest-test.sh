#!/bin/bash
# S6-D70: fix HPM ASCII index decoding in init/restore on D66 base.
# PASS: HPM1 powerup and physical USB-C input. No input evidence = unverified.
# First test resumes D68 proxy after D69 SSPS; fresh-boot proof is separate.
# D61/D62 framebuffer-tile and saved-report arrays are compiled out to restore
# the exact successful D59 outer-image size and reduce Project Mu entry variance.
# Safety: no reboot and one chainload; caller performs one bootstrap reboot.
# Baud remains fixed at 115200.
set -eu
ROOT=/Volumes/X31/NWOAS
M1N1="$ROOT/m1n1_windows"
DEV=/dev/cu.usbmodemC07HL05SQ6NY1
PY="$M1N1/.venv-hv/bin/python3"
HV="$M1N1/build/m1n1-d70-usbc-hpm-index.bin"
PAYLOAD="$M1N1/m1n1-payload-iort-noleafdma-rering-runtime-dart-v1.bin"
HOOK="$M1N1/tahoe_dcp_guest_hook.py"
MODULE="$M1N1/pcie_emul-exp5-evtdump.py"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/tahoe-dcp-d70-usbc-hpm-index-hidwatch-$(date '+%Y%m%d-%H%M%S').XXXXXX")

export M1N1DEVICE="$DEV" M1N1_KEEP_BAUD=1 PYTHONDONTWRITEBYTECODE=1
export PYTHONPATH="$M1N1/proxyclient:$ROOT/nwoas_scripts"
export NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000 NWOAS_LOG="$LOG"
export NWOAS_DCP_LOG="$LOG.dcp" NWOAS_HV_IMAGE="$HV"

echo "Log: $LOG"
[ -e "$DEV" ] || { echo "STOP: direct CDC missing"; exit 1; }
[ "$(stat -f %z "$HV")" -eq 2129920 ]
[ "$(shasum -a 256 "$HV" | awk '{print $1}')" = "31faf3a63bf8374b4270a2685630b80052f7a4e2b20e489e76cf9a375597c3b3" ]
[ "$(stat -f %z "$PAYLOAD")" -eq 32342016 ]
if /usr/sbin/lsof -t "$DEV" >/dev/null 2>&1; then
    echo "STOP: serial busy"
    exit 1
fi
shasum -a 256 "$HV" "$PAYLOAD" "$HOOK" "$MODULE" >> "$LOG"
echo "[DESIGN] D70 HPM ASCII index correction on D66; D69 PD already awake" | tee -a "$LOG"

"$PY" -u "$M1N1/proxyclient/tools/chainload.py" -r "$HV" >> "$LOG" 2>&1
if ! grep -qa 'Proxy is alive again\|WARNING: chainload confirm' "$LOG"; then
    echo "STOP: chainload did not reach its confirmation stage; no retry" | tee -a "$LOG"
    exit 2
fi

exec "$PY" -u "$M1N1/proxyclient/tools/run_guest.py" -r \
    --pre-init-script "$HOOK" -m "$MODULE" "$PAYLOAD" >> "$LOG" 2>&1
