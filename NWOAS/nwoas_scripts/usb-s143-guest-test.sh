#!/bin/bash
# S143: S141 storage transfer size plus completion-DPC handoff throttling.
set -eu
ROOT=/Volumes/X31/NWOAS
M1N1="$ROOT/m1n1_windows"
DEV=/dev/cu.usbmodemC07HL05SQ6NY1
PY="$M1N1/.venv-hv/bin/python3"
HV="$M1N1/build/m1n1-s141-1m-nvme.bin"
PAYLOAD="$M1N1/m1n1-payload-s139-8cpu-usba.bin"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/usb-s143-$(date '+%Y%m%d-%H%M%S').XXXXXX")

export M1N1DEVICE="$DEV" M1N1_KEEP_BAUD=1 PYTHONDONTWRITEBYTECODE=1
export PYTHONPATH="$M1N1/proxyclient:$ROOT/nwoas_scripts"
export NWOAS_LINK_DIR="$LOG.link"
export NWOAS_CONTROLLER_DIR="$ROOT/nwoas_scripts/nvme-s143"
export NWOAS_MAX_TRANSFER=1048576
export NWOAS_EXCLUDE_WINDOW=1 NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000 NWOAS_LOG="$LOG"
export NWOAS_DCP_LOG="$LOG.dcp" NWOAS_HV_IMAGE="$HV"

echo "Log: $LOG"
[ -e "$DEV" ] || { echo 'STOP: direct CDC missing'; exit 1; }
[ "$(stat -f %z "$HV")" -eq 2146304 ]
[ "$(shasum -a 256 "$HV" | awk '{print $1}')" = e3be86394ccc2d9c779147244d45bf9068d9118a3e2d93a22ad3268b8f9dbdfd ]
[ "$(stat -f %z "$PAYLOAD")" -eq 32342016 ]
[ "$(shasum -a 256 "$PAYLOAD" | awk '{print $1}')" = 5dc5b108f5b8cc2a6cc80f53881b27696d5b695732683979e5b6948f5c18d953 ]
if /usr/sbin/lsof -t "$DEV" >/dev/null 2>&1; then echo 'STOP: serial busy'; exit 1; fi
shasum -a 256 "$HV" "$PAYLOAD" "$ROOT/nwoas_scripts/cpufreq-s140/init.py" "$ROOT/nwoas_scripts/nvme-s143/controller.py" >> "$LOG"
echo '[S143] S141 1 MiB NVMe plus deferred completion-DPC submissions' | tee -a "$LOG"

"$PY" -u "$ROOT/nwoas_scripts/recovery-s126/proxy_ready.py" >> "$LOG" 2>&1
"$PY" -u "$M1N1/proxyclient/tools/chainload.py" -r "$HV" >> "$LOG" 2>&1
grep -qa 'Proxy is alive again\|WARNING: chainload confirm' "$LOG" || exit 2
exec "$PY" -u "$M1N1/proxyclient/tools/run_guest.py" -r \
    --pre-init-script "$M1N1/tahoe_dcp_guest_hook.py" \
    -m "$ROOT/nwoas_scripts/cpufreq-s140/init.py" \
    -m "$ROOT/nwoas_scripts/rescue-s138/pcie_usba_only.py" \
    -m "$ROOT/nwoas_scripts/smp-s133/pmgr_gate.py" \
    -m "$ROOT/nwoas_scripts/nvme-s130/guest_module.py" \
    -m "$ROOT/nwoas_scripts/uefi-s125/link_module.py" "$PAYLOAD" >> "$LOG" 2>&1
