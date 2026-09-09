#!/bin/bash
# S96: NVMe on isolated PCI root; writes only inside WINTEST LBA 53839104-59968511. PASS: Windows Identify/Read observed.
# No shared DART table or ERST coherence experiment; D79 late diagnostics kept.
# PASS: no HSE, slot1 non-control completions and physical cursor response.
# Safety: non-debug USB1 only; bounded slot1/ring checks, no disk writes,
# one caller bootstrap/one chainload, fixed115200, SIGTERM stop.
set -eu
ROOT=/Volumes/X31/NWOAS
M1N1="$ROOT/m1n1_windows"
DEV=/dev/cu.usbmodemC07HL05SQ6NY1
PY="$M1N1/.venv-hv/bin/python3"
HV="$M1N1/build/m1n1-s101-nvme-zerocopy.bin"
PAYLOAD="$M1N1/m1n1-payload-s93-nvme.bin"
HOOK="$M1N1/tahoe_dcp_guest_hook.py"
MODULE="$M1N1/pcie_emul-d81-usbc-payload.py"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/nvme-s101-$(date '+%Y%m%d-%H%M%S').XXXXXX")

export M1N1DEVICE="$DEV" M1N1_KEEP_BAUD=1 PYTHONDONTWRITEBYTECODE=1
export PYTHONPATH="$M1N1/proxyclient:$ROOT/nwoas_scripts"
export NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000 NWOAS_LOG="$LOG"
export NWOAS_DCP_LOG="$LOG.dcp" NWOAS_HV_IMAGE="$HV"

echo "Log: $LOG"
[ -e "$DEV" ] || { echo "STOP: direct CDC missing"; exit 1; }
[ "$(stat -f %z "$HV")" -eq 2129920 ]
[ "$(shasum -a 256 "$HV" | awk '{print $1}')" = "484a225174a30e328dcc1234600829ea0561a7bcbde24cbe15767b6a8954c507" ]
[ "$(stat -f %z "$PAYLOAD")" -eq 32342016 ]
if /usr/sbin/lsof -t "$DEV" >/dev/null 2>&1; then
    echo "STOP: serial busy"
    exit 1
fi
shasum -a 256 "$HV" "$PAYLOAD" "$HOOK" "$MODULE" >> "$LOG"
echo "[DESIGN] S93 read-only NVMe transport, Windows detection hardware-unverified" | tee -a "$LOG"

"$PY" -u "$M1N1/proxyclient/tools/chainload.py" -r "$HV" >> "$LOG" 2>&1
if ! grep -qa 'Proxy is alive again\|WARNING: chainload confirm' "$LOG"; then
    echo "STOP: chainload did not reach its confirmation stage; no retry" | tee -a "$LOG"
    exit 2
fi

exec "$PY" -u "$M1N1/proxyclient/tools/run_guest.py" -r \
    --pre-init-script "$HOOK" -m "$MODULE" -m "$ROOT/nwoas_scripts/nvme-s93/guest_module.py" "$PAYLOAD" >> "$LOG" 2>&1
