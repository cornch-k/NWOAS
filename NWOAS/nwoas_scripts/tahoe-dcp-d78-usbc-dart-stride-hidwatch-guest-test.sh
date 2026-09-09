#!/bin/bash
# S6-D78: USB1 DART translation of guest low-window buffers, before Windows RS.
# ONLY change from D77: correct DART instance stride to 0x80000.
# PASS: mapping logs, no HSE, USB-C transfer completions + physical input.
# Safety: USB0/debug DART untouched; halted controller only; one chainload,
# caller-owned one bootstrap, 115200, no automatic retry; no disk writes.
set -eu
ROOT=/Volumes/X31/NWOAS
M1N1="$ROOT/m1n1_windows"
DEV=/dev/cu.usbmodemC07HL05SQ6NY1
PY="$M1N1/.venv-hv/bin/python3"
HV="$M1N1/build/m1n1-d78-usbc-dart-stride.bin"
PAYLOAD="$M1N1/m1n1-payload-iort-noleafdma-rering-runtime-dart-v1.bin"
HOOK="$M1N1/tahoe_dcp_guest_hook.py"
MODULE="$M1N1/pcie_emul-d77-usbc-dma.py"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/tahoe-dcp-d78-usbc-dart-stride-hidwatch-$(date '+%Y%m%d-%H%M%S').XXXXXX")

export M1N1DEVICE="$DEV" M1N1_KEEP_BAUD=1 PYTHONDONTWRITEBYTECODE=1
export PYTHONPATH="$M1N1/proxyclient:$ROOT/nwoas_scripts"
export NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000 NWOAS_LOG="$LOG"
export NWOAS_DCP_LOG="$LOG.dcp" NWOAS_HV_IMAGE="$HV"

echo "Log: $LOG"
[ -e "$DEV" ] || { echo "STOP: direct CDC missing"; exit 1; }
[ "$(stat -f %z "$HV")" -eq 2129920 ]
[ "$(shasum -a 256 "$HV" | awk '{print $1}')" = "988bee7d8ff03bb196bbcc80d3801e0835e09c1a9c86c62b61ed23fba481adb6" ]
[ "$(stat -f %z "$PAYLOAD")" -eq 32342016 ]
if /usr/sbin/lsof -t "$DEV" >/dev/null 2>&1; then
    echo "STOP: serial busy"
    exit 1
fi
shasum -a 256 "$HV" "$PAYLOAD" "$HOOK" "$MODULE" >> "$LOG"
echo "[DESIGN] D78 USB1 DMA low-window translation before Windows Run; input hardware-unverified" | tee -a "$LOG"

"$PY" -u "$M1N1/proxyclient/tools/chainload.py" -r "$HV" >> "$LOG" 2>&1
if ! grep -qa 'Proxy is alive again\|WARNING: chainload confirm' "$LOG"; then
    echo "STOP: chainload did not reach its confirmation stage; no retry" | tee -a "$LOG"
    exit 2
fi

exec "$PY" -u "$M1N1/proxyclient/tools/run_guest.py" -r \
    --pre-init-script "$HOOK" -m "$MODULE" "$PAYLOAD" >> "$LOG" 2>&1
