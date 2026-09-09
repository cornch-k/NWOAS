#!/bin/bash
# S6-D43: D42's AC64=0 low-DMA allocation plus a UEFI payload whose live
# apcie-DART L1/L2 tables use runtime-reserved pages. This prevents Windows
# from reclaiming the tables after ExitBootServices.
# PASS: FL32 marker; kernel DCBAAP/ERSTBA/CRCR below 4GB; DARTDIAG maps all
# three to the low-window backing; then a real controller event and ERDP move.
# Cursor movement remains the final HID proof.
# Safety: no reboot and one chainload; caller performs one bootstrap reboot.
# Baud remains fixed at 115200.
set -eu
ROOT=/Volumes/X31/NWOAS
M1N1="$ROOT/m1n1_windows"
DEV=/dev/cu.usbmodemC07HL05SQ6NY1
PY="$M1N1/.venv-hv/bin/python3"
HV="$M1N1/build/m1n1.bin"
PAYLOAD="$M1N1/m1n1-payload-iort-noleafdma-rering-runtime-dart-v1.bin"
HOOK="$M1N1/tahoe_dcp_guest_hook.py"
MODULE="$M1N1/pcie_emul-exp5-evtdump.py"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/tahoe-dcp-fl32-runtime-dart-$(date '+%Y%m%d-%H%M%S').XXXXXX")

export M1N1DEVICE="$DEV" M1N1_KEEP_BAUD=1 PYTHONDONTWRITEBYTECODE=1
export PYTHONPATH="$M1N1/proxyclient:$ROOT/nwoas_scripts"
export NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000 NWOAS_LOG="$LOG"
export NWOAS_DCP_LOG="$LOG.dcp" NWOAS_HV_IMAGE="$HV"

echo "Log: $LOG"
[ -e "$DEV" ] || { echo "STOP: direct CDC missing"; exit 1; }
[ "$(stat -f %z "$HV")" -gt 2000000 ]
[ "$(stat -f %z "$PAYLOAD")" -eq 32342016 ]
if /usr/sbin/lsof -t "$DEV" >/dev/null 2>&1; then
    echo "STOP: serial busy"
    exit 1
fi
shasum -a 256 "$HV" "$PAYLOAD" "$HOOK" "$MODULE" >> "$LOG"
echo "[DESIGN] D43 AC64=0 plus runtime-reserved live DART tables" | tee -a "$LOG"

"$PY" -u "$M1N1/proxyclient/tools/chainload.py" -r "$HV" >> "$LOG" 2>&1
if ! grep -qa 'Proxy is alive again\|WARNING: chainload confirm' "$LOG"; then
    echo "STOP: chainload did not reach its confirmation stage; no retry" | tee -a "$LOG"
    exit 2
fi

exec "$PY" -u "$M1N1/proxyclient/tools/run_guest.py" -r \
    --pre-init-script "$HOOK" -m "$MODULE" "$PAYLOAD" >> "$LOG" 2>&1
