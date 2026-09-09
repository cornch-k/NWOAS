#!/bin/bash
# S158: S150 NVMe link plus deterministic visibility for real FL1100 events.
set -eu
ROOT=/Volumes/X31/NWOAS
M1N1="$ROOT/m1n1_windows"
DEV=/dev/cu.usbmodemC07HL05SQ6NY1
VUART=/dev/cu.usbmodemC07HL05SQ6NY3
PY="$M1N1/.venv-hv/bin/python3"
HV="$ROOT/m1n1_windows-s159/build/m1n1-s160-local-mask.bin"
PAYLOAD="$M1N1/m1n1-payload-s139-8cpu-usba.bin"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/usb-s160-$(date '+%Y%m%d-%H%M%S').XXXXXX")

export NWOAS_HV_READ_TIMEOUT=30
export M1N1DEVICE="$DEV" M1N1_KEEP_BAUD=1 M1N1_SPLIT_CONSOLE=1 PYTHONDONTWRITEBYTECODE=1
export PYTHONPATH="$M1N1/proxyclient:$ROOT/nwoas_scripts"
export NWOAS_LINK_DIR="$LOG.link"
export NWOAS_CONTROLLER_DIR="$ROOT/nwoas_scripts/nvme-s139"
export NWOAS_MAX_TRANSFER=1048576 NWOAS_NVME_FAST_MASK=1
export NWOAS_EXCLUDE_WINDOW=1 NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000 NWOAS_LOG="$LOG"
export NWOAS_DCP_LOG="$LOG.dcp" NWOAS_HV_IMAGE="$HV"

echo "Log: $LOG"
[ -e "$DEV" ] || { echo 'STOP: direct CDC proxy missing'; exit 1; }
[ -e "$VUART" ] || { echo 'STOP: direct CDC diagnostic pipe missing'; exit 1; }
[ "$(stat -f %z "$HV")" -eq 2146304 ]
[ "$(shasum -a 256 "$HV" | awk '{print $1}')" = f95bee4537f0d961b54ea3546af34100a8909e4ef9491066fc051a8d8c0ae0c7 ]
[ "$(shasum -a 256 "$ROOT/nwoas_scripts/nvme-s160/guest_module.py" | awk '{print $1}')" = 5f8cf61b3ccc32fc53a40f3ea14aa3ffa5ffbd34f672e1e7092e6eb291db3c05 ]
[ "$(stat -f %z "$PAYLOAD")" -eq 32342016 ]
[ "$(shasum -a 256 "$PAYLOAD" | awk '{print $1}')" = 5dc5b108f5b8cc2a6cc80f53881b27696d5b695732683979e5b6948f5c18d953 ]
if /usr/sbin/lsof -t "$DEV" "$VUART" >/dev/null 2>&1; then echo 'STOP: CDC pipe busy'; exit 1; fi
shasum -a 256 "$HV" "$PAYLOAD" "$ROOT/nwoas_scripts/cpufreq-s140/init.py" \
    "$ROOT/nwoas_scripts/nvme-s139/controller.py" >> "$LOG"
echo '[S160] Target local INTMS/INTMC, inline SQ baseline, 8 cores USB-A' | tee -a "$LOG"

VUART_PID=
cleanup() {
    if [ -n "$VUART_PID" ]; then
        kill "$VUART_PID" >/dev/null 2>&1 || true
        wait "$VUART_PID" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT HUP TERM
trap ':' INT

"$PY" -u "$ROOT/nwoas_scripts/recovery-s126/proxy_ready.py" >> "$LOG" 2>&1
"$PY" -u "$M1N1/proxyclient/tools/chainload.py" -r "$HV" >> "$LOG" 2>&1
grep -qa 'Proxy is alive again\|WARNING: chainload confirm' "$LOG" || exit 2
"$PY" -u "$ROOT/nwoas_scripts/baud-s144/capture_vuart.py" \
    --port "$VUART" --output "$LOG.vuart" --ignore-sigint &
VUART_PID=$!

"$PY" -u "$M1N1/proxyclient/tools/run_guest.py" -r \
    --pre-init-script "$M1N1/tahoe_dcp_guest_hook.py" \
    -m "$ROOT/nwoas_scripts/cpufreq-s140/init.py" \
    -m "$ROOT/nwoas_scripts/rescue-s138/pcie_usba_only.py" \
    -m "$ROOT/nwoas_scripts/smp-s133/pmgr_gate.py" \
    -m "$ROOT/nwoas_scripts/nvme-s160/guest_module.py" \
    -m "$ROOT/nwoas_scripts/uefi-s125/link_module.py" "$PAYLOAD" >> "$LOG" 2>&1
