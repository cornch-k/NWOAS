#!/bin/bash
# S136: isolate vGIC SPI-disable mask polarity on the S133/S131 payload.
set -eu
ROOT=/Volumes/X31/NWOAS
M1N1="$ROOT/m1n1_windows"
DEV=/dev/cu.usbmodemC07HL05SQ6NY1
PY="$M1N1/.venv-hv/bin/python3"
HV="$M1N1/build/m1n1-s136-vgic-mask.bin"
PAYLOAD="$M1N1/m1n1-payload-s131-8cpu.bin"
HOOK="$M1N1/tahoe_dcp_guest_hook.py"
MODULE="$M1N1/pcie_emul-d81-usbc-payload.py"
GATE="$ROOT/nwoas_scripts/smp-s133/pmgr_gate.py"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/usb-s136-$(date '+%Y%m%d-%H%M%S').XXXXXX")

export M1N1DEVICE="$DEV" M1N1_KEEP_BAUD=1 PYTHONDONTWRITEBYTECODE=1
export PYTHONPATH="$M1N1/proxyclient:$ROOT/nwoas_scripts"
export NWOAS_LINK_DIR="$LOG.link"
export NWOAS_EXCLUDE_WINDOW=1 NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000 NWOAS_LOG="$LOG"
export NWOAS_DCP_LOG="$LOG.dcp" NWOAS_HV_IMAGE="$HV"

echo "Log: $LOG"
[ -e "$DEV" ] || { echo "STOP: direct CDC missing"; exit 1; }
[ "$(stat -f %z "$HV")" -eq 2146304 ]
[ "$(shasum -a 256 "$HV" | awk '{print $1}')" = "b4a2f0d65e2a784da8b4ca2d944d5a2e2300df2c546a48d55f9ea5a9118130be" ]
[ "$(stat -f %z "$PAYLOAD")" -eq 32342016 ]
[ "$(shasum -a 256 "$PAYLOAD" | awk '{print $1}')" = "4e87a1d2bf362e309884a0539c6023c49c6da01bb77894a35109a88e430389b1" ]
if /usr/sbin/lsof -t "$DEV" >/dev/null 2>&1; then
    echo "STOP: serial busy"
    exit 1
fi
shasum -a 256 "$HV" "$PAYLOAD" "$HOOK" "$MODULE" "$GATE" >> "$LOG"
echo "[S136] S133 MADT8/PSCI + corrected vGIC SPI disable polarity; baud=115200" | tee -a "$LOG"

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
