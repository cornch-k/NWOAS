#!/bin/bash
# S6-D76: one-shot HSE slot1 endpoint and transfer-ring read-only snapshot.
# ONLY change from D75 is diagnostics; no claimed input fix.
# PASS: bounded capture without faults; physical Trackpad input remains separate.
# Safety: one chainload, caller-owned one bootstrap, 115200, no automatic retry.
set -eu
ROOT=/Volumes/X31/NWOAS
M1N1="$ROOT/m1n1_windows"
DEV=/dev/cu.usbmodemC07HL05SQ6NY1
PY="$M1N1/.venv-hv/bin/python3"
HV="$M1N1/build/m1n1-d76-usbc-hse-endpoints.bin"
PAYLOAD="$M1N1/m1n1-payload-iort-noleafdma-rering-runtime-dart-v1.bin"
HOOK="$M1N1/tahoe_dcp_guest_hook.py"
MODULE="$M1N1/pcie_emul-exp5-evtdump.py"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/tahoe-dcp-d76-usbc-hse-endpoints-hidwatch-$(date '+%Y%m%d-%H%M%S').XXXXXX")

export M1N1DEVICE="$DEV" M1N1_KEEP_BAUD=1 PYTHONDONTWRITEBYTECODE=1
export PYTHONPATH="$M1N1/proxyclient:$ROOT/nwoas_scripts"
export NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000 NWOAS_LOG="$LOG"
export NWOAS_DCP_LOG="$LOG.dcp" NWOAS_HV_IMAGE="$HV"

echo "Log: $LOG"
[ -e "$DEV" ] || { echo "STOP: direct CDC missing"; exit 1; }
[ "$(stat -f %z "$HV")" -eq 2129920 ]
[ "$(shasum -a 256 "$HV" | awk '{print $1}')" = "0f5edce116793580ed6a0d548194aa18c4ac3bb6bbeb26822805ebdbc3cedaf6" ]
[ "$(stat -f %z "$PAYLOAD")" -eq 32342016 ]
if /usr/sbin/lsof -t "$DEV" >/dev/null 2>&1; then
    echo "STOP: serial busy"
    exit 1
fi
shasum -a 256 "$HV" "$PAYLOAD" "$HOOK" "$MODULE" >> "$LOG"
echo "[DESIGN] D76 one HSE snapshot of USB-C event ring and DART fault state on D72" | tee -a "$LOG"

"$PY" -u "$M1N1/proxyclient/tools/chainload.py" -r "$HV" >> "$LOG" 2>&1
if ! grep -qa 'Proxy is alive again\|WARNING: chainload confirm' "$LOG"; then
    echo "STOP: chainload did not reach its confirmation stage; no retry" | tee -a "$LOG"
    exit 2
fi

exec "$PY" -u "$M1N1/proxyclient/tools/run_guest.py" -r \
    --pre-init-script "$HOOK" -m "$MODULE" "$PAYLOAD" >> "$LOG" 2>&1
