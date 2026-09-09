#!/bin/bash
# S6-D37: ONLY display variable versus D36 is one 720p mode/surface reapply
# after Tahoe's first IOAV hotplug/republication cycle. Guest BootArgs remain
# 1280x720/5120/32 and the D36 low-event-ring diagnostic remains unchanged.
# PASS (display): physical color bars survive the five-second serviced hold.
# PASS (USB seed): a controller-authored cycle-valid type-34 TRB is logged.
# Safety: no reboot and one chainload; caller performs the separately logged
# single bootstrap reboot. Baud stays 115200 through M1N1_KEEP_BAUD=1.
set -eu
ROOT=/Volumes/X31/NWOAS
M1N1="$ROOT/m1n1_windows"
DEV=/dev/cu.usbmodemC07HL05SQ6NY1
PY="$M1N1/.venv-hv/bin/python3"
HV="$M1N1/build/m1n1.bin"
PAYLOAD="$M1N1/m1n1-payload-iort-noleafdma-rering-v1.bin"
HOOK="$M1N1/tahoe_dcp_guest_hook.py"
MODULE="$M1N1/pcie_emul-exp5-evtdump.py"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/tahoe-dcp-reapply-$(date '+%Y%m%d-%H%M%S').XXXXXX")

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
echo "[DESIGN] D37 one post-hotplug display reapply; 5s serviced color-bar hold" | tee -a "$LOG"

"$PY" -u "$M1N1/proxyclient/tools/chainload.py" -r "$HV" >> "$LOG" 2>&1
if ! grep -qa 'Proxy is alive again\|WARNING: chainload confirm' "$LOG"; then
    echo "STOP: chainload did not reach its confirmation stage; no retry" | tee -a "$LOG"
    exit 2
fi

exec "$PY" -u "$M1N1/proxyclient/tools/run_guest.py" -r \
    --pre-init-script "$HOOK" -m "$MODULE" "$PAYLOAD" >> "$LOG" 2>&1
