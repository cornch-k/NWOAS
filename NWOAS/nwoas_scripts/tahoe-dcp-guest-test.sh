#!/bin/bash
# S6-D32: no reboot and no chainload.  Run the existing Windows guest on the
# installed D4 HV, configure Tahoe DCP only at the final pre-guest boundary,
# and leave DCP/AFK/RTKit running across the handoff.
set -eu
ROOT=/Volumes/X31/NWOAS
M1N1="$ROOT/m1n1_windows"
DEV=/dev/cu.usbmodemC07HL05SQ6NY1
PY="$M1N1/.venv-hv/bin/python3"
PAYLOAD="${NWOAS_PAYLOAD:-$M1N1/m1n1-payload-iort-noleafdma-v1.bin}"
HOOK="$M1N1/tahoe_dcp_guest_hook.py"
MODULE="$M1N1/pcie_emul-exp5-evtdump.py"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/tahoe-dcp-guest-$(date '+%Y%m%d-%H%M%S').XXXXXX")
export M1N1DEVICE="$DEV" M1N1_KEEP_BAUD=1 PYTHONDONTWRITEBYTECODE=1
export PYTHONPATH="$M1N1/proxyclient:$ROOT/nwoas_scripts"
export NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000 NWOAS_LOG="$LOG"
export NWOAS_DCP_LOG="$LOG.dcp"
echo "Log: $LOG"
[ -e "$DEV" ] || { echo "STOP: direct CDC missing"; exit 1; }
[ "$(md5 -q "$ROOT/experiments/tahoe-afk-20260906/m1n1-dcp-defer.bin")" = 7bec0225edc101857be3b7f195dd7357 ]
if /usr/sbin/lsof -t "$DEV" >/dev/null 2>&1; then
    echo "STOP: serial busy"
    exit 1
fi
shasum "$PAYLOAD" "$HOOK" "$MODULE" >> "$LOG"
echo "[DESIGN] direct run_guest payload=$(basename "$PAYLOAD"), early DCP boot + pre-entry mode/swap, no shutdown/chainload" | tee -a "$LOG"
exec "$PY" -u "$M1N1/proxyclient/tools/run_guest.py" -r \
    --pre-init-script "$HOOK" -m "$MODULE" "$PAYLOAD" >> "$LOG" 2>&1
