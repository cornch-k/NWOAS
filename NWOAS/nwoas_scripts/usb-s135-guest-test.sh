#!/bin/bash
# S135: S133 eight-core single-owner startup + corrected SMBIOS CurrentSpeed.
set -eu
ROOT=/Volumes/X31/NWOAS
M1N1="$ROOT/m1n1_windows"
DEV=/dev/cu.usbmodemC07HL05SQ6NY1
PY="$M1N1/.venv-hv/bin/python3"
HV="$M1N1/build/m1n1-s133-single-owner-smp.bin"
PAYLOAD="$M1N1/m1n1-payload-s135-8cpu-speed.bin"
HOOK="$M1N1/tahoe_dcp_guest_hook.py"
MODULE="$M1N1/pcie_emul-d81-usbc-payload.py"
GATE="$ROOT/nwoas_scripts/smp-s133/pmgr_gate.py"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/usb-s135-$(date '+%Y%m%d-%H%M%S').XXXXXX")

export M1N1DEVICE="$DEV" PYTHONDONTWRITEBYTECODE=1
if [ -n "${M1N1_BAUD:-}" ]; then
    unset M1N1_KEEP_BAUD || true
    export M1N1_BAUD
    BAUD_DESC="$M1N1_BAUD"
else
    export M1N1_KEEP_BAUD=1
    BAUD_DESC=115200
fi
export PYTHONPATH="$M1N1/proxyclient:$ROOT/nwoas_scripts"
export NWOAS_LINK_DIR="$LOG.link"
export NWOAS_EXCLUDE_WINDOW=1 NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000 NWOAS_LOG="$LOG"
export NWOAS_DCP_LOG="$LOG.dcp" NWOAS_HV_IMAGE="$HV"

echo "Log: $LOG"
[ -e "$DEV" ] || { echo "STOP: direct CDC missing"; exit 1; }
[ "$(stat -f %z "$HV")" -eq 2146304 ]
[ "$(shasum -a 256 "$HV" | awk '{print $1}')" = "e8b9590d0ec3b55e710623e53d735a70d1a353ba5d929de024a005cb2e2ff153" ]
[ "$(stat -f %z "$PAYLOAD")" -eq 32342016 ]
[ "$(shasum -a 256 "$PAYLOAD" | awk '{print $1}')" = "73f9623d6cf8060c387e2a87119391ed66a49f93ec70b4cd628dc40a8df93e11" ]
if /usr/sbin/lsof -t "$DEV" >/dev/null 2>&1; then
    echo "STOP: serial busy"
    exit 1
fi
shasum -a 256 "$HV" "$PAYLOAD" "$HOOK" "$MODULE" "$GATE" >> "$LOG"
echo "[S135] S133 MADT8/PSCI + SMBIOS CurrentSpeed 3228 MHz + quiet S130 NVMe; baud=$BAUD_DESC" | tee -a "$LOG"

"$PY" -u "$ROOT/nwoas_scripts/recovery-s126/proxy_ready.py" >> "$LOG" 2>&1
"$PY" -u "$M1N1/proxyclient/tools/chainload.py" -r "$HV" >> "$LOG" 2>&1
if ! grep -qa 'Proxy is alive again\|WARNING: chainload confirm' "$LOG"; then
    echo "STOP: chainload did not reach its confirmation stage; no retry" | tee -a "$LOG"
    exit 2
fi

exec "$PY" -u "$M1N1/proxyclient/tools/run_guest.py" -r \
    --pre-init-script "$HOOK" -m "$MODULE" -m "$GATE" \
    -m "$ROOT/nwoas_scripts/nvme-s130/guest_module.py" \
    -m "$ROOT/nwoas_scripts/uefi-s125/link_module.py" "$PAYLOAD" >> "$LOG" 2>&1
