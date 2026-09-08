#!/bin/bash
# S114 Legacy FL1100 cache sweep exclusion experiment + unchanged S102 UEFI, Tahoe J274.
# Use HASH113 media with no S100.TAG. USB-source hash only, automatic WinPE shutdown.
# Windows-zone writes are enabled; GPT updates pass the existing validation guard.
# USB S100.TAG arms destructive trial-partition reformat/install. S106.TAG instead
# arms file hashing via the media's RunSynchronous hook; verify media before boot.
# Fixed CDC 115200, one chainload, no automatic retry. Stop with SIGTERM, not SIGINT.
set -eu
ROOT=/Volumes/X31/NWOAS
M1N1="$ROOT/m1n1_windows"
DEV=/dev/cu.usbmodemC07HL05SQ6NY1
PY="$M1N1/.venv-hv/bin/python3"
HV="$M1N1/build/m1n1-s114-nosweep.bin"
PAYLOAD="$M1N1/m1n1-payload-s102-exclwin.bin"
HOOK="$M1N1/tahoe_dcp_guest_hook.py"
MODULE="$M1N1/pcie_emul-d81-usbc-payload.py"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/usb-s114-$(date '+%Y%m%d-%H%M%S').XXXXXX")

export M1N1DEVICE="$DEV" M1N1_KEEP_BAUD=1 PYTHONDONTWRITEBYTECODE=1
export PYTHONPATH="$M1N1/proxyclient:$ROOT/nwoas_scripts"
export NWOAS_EXCLUDE_WINDOW=1 NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000 NWOAS_LOG="$LOG"
export NWOAS_DCP_LOG="$LOG.dcp" NWOAS_HV_IMAGE="$HV"

echo "Log: $LOG"
[ -e "$DEV" ] || { echo "STOP: direct CDC missing"; exit 1; }
[ "$(stat -f %z "$HV")" -eq 2129920 ]
[ "$(shasum -a 256 "$HV" | awk '{print $1}')" = "0f09061bc8ecdba7cb6154b196303cd62611887032c05f2cef19799bb55d0450" ]
[ "$(stat -f %z "$PAYLOAD")" -eq 32342016 ]
if /usr/sbin/lsof -t "$DEV" >/dev/null 2>&1; then
    echo "STOP: serial busy"
    exit 1
fi
shasum -a 256 "$HV" "$PAYLOAD" "$HOOK" "$MODULE" >> "$LOG"
echo "[S114] Legacy FL1100 doorbell cache sweep disabled experiment; USB-only hash diagnostic; original NVMe guard remains enabled" | tee -a "$LOG"

"$PY" -u "$M1N1/proxyclient/tools/chainload.py" -r "$HV" >> "$LOG" 2>&1
if ! grep -qa 'Proxy is alive again\|WARNING: chainload confirm' "$LOG"; then
    echo "STOP: chainload did not reach its confirmation stage; no retry" | tee -a "$LOG"
    exit 2
fi

exec "$PY" -u "$M1N1/proxyclient/tools/run_guest.py" -r \
    --pre-init-script "$HOOK" -m "$MODULE" -m "$ROOT/nwoas_scripts/nvme-s93/guest_module.py" "$PAYLOAD" >> "$LOG" 2>&1
