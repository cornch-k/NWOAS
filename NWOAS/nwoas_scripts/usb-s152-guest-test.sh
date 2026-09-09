#!/bin/bash
# S152: S151 NVMe/FL1100 baseline plus D83 USB-C and corrected CPU speed reporting.
set -eu
ROOT=/Volumes/X31/NWOAS
M1N1="$ROOT/m1n1_windows"
DEV=/dev/cu.usbmodemC07HL05SQ6NY1
VUART=/dev/cu.usbmodemC07HL05SQ6NY3
PY="$M1N1/.venv-hv/bin/python3"
HV="$M1N1/build/m1n1-s151-fl1100-real-event-invalidate.bin"
PAYLOAD="$M1N1/m1n1-payload-s135-8cpu-speed.bin"
USBC="$M1N1/pcie_emul-d81-usbc-payload.py"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/usb-s152-$(date '+%Y%m%d-%H%M%S').XXXXXX")

export NWOAS_HV_READ_TIMEOUT=30
export M1N1DEVICE="$DEV" M1N1_KEEP_BAUD=1 M1N1_SPLIT_CONSOLE=1 PYTHONDONTWRITEBYTECODE=1
export PYTHONPATH="$M1N1/proxyclient:$ROOT/nwoas_scripts"
export NWOAS_LINK_DIR="$LOG.link"
export NWOAS_CONTROLLER_DIR="$ROOT/nwoas_scripts/nvme-s139"
export NWOAS_MAX_TRANSFER=1048576
export NWOAS_EXCLUDE_WINDOW=1 NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000 NWOAS_WIN_SKEW=0x34000
export NWOAS_LOG="$LOG"
export NWOAS_DCP_LOG="$LOG.dcp" NWOAS_HV_IMAGE="$HV"

echo "Log: $LOG"
[ -e "$DEV" ] || { echo 'STOP: direct CDC proxy missing'; exit 1; }
[ -e "$VUART" ] || { echo 'STOP: direct CDC diagnostic pipe missing'; exit 1; }
[ "$(stat -f %z "$HV")" -eq 2146304 ]
[ "$(shasum -a 256 "$HV" | awk '{print $1}')" = 00ae83083970ba7ebf7544f40ad22d888e3602d2f10e5d966f4f1c902b49865a ]
[ "$(stat -f %z "$PAYLOAD")" -eq 32342016 ]
[ "$(shasum -a 256 "$PAYLOAD" | awk '{print $1}')" = 73f9623d6cf8060c387e2a87119391ed66a49f93ec70b4cd628dc40a8df93e11 ]
[ "$(shasum -a 256 "$USBC" | awk '{print $1}')" = ee1027d1c868114fb09bafbdb19b50b312b2c3fc456ee3f9309ccbcfa9f07f64 ]
if /usr/sbin/lsof -t "$DEV" "$VUART" >/dev/null 2>&1; then echo 'STOP: CDC pipe busy'; exit 1; fi
shasum -a 256 "$HV" "$PAYLOAD" "$ROOT/nwoas_scripts/cpufreq-s140/init.py" \
    "$ROOT/nwoas_scripts/nvme-s139/controller.py" "$USBC" >> "$LOG"
echo '[S152] S151 NVMe/FL1100 + D83 USB-C + SMBIOS 3228 MHz' | tee -a "$LOG"

VUART_PID=
cleanup() {
    if [ -n "$VUART_PID" ]; then
        kill "$VUART_PID" >/dev/null 2>&1 || true
        wait "$VUART_PID" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT HUP INT TERM

"$PY" -u "$ROOT/nwoas_scripts/recovery-s126/proxy_ready.py" >> "$LOG" 2>&1
"$PY" -u "$M1N1/proxyclient/tools/chainload.py" -r "$HV" >> "$LOG" 2>&1
grep -qa 'Proxy is alive again\|WARNING: chainload confirm' "$LOG" || exit 2
"$PY" -u "$ROOT/nwoas_scripts/baud-s144/capture_vuart.py" \
    --port "$VUART" --output "$LOG.vuart" &
VUART_PID=$!

"$PY" -u "$M1N1/proxyclient/tools/run_guest.py" -r \
    --pre-init-script "$M1N1/tahoe_dcp_guest_hook.py" \
    -m "$ROOT/nwoas_scripts/cpufreq-s140/init.py" \
    -m "$USBC" \
    -m "$ROOT/nwoas_scripts/smp-s133/pmgr_gate.py" \
    -m "$ROOT/nwoas_scripts/nvme-s130/guest_module.py" \
    -m "$ROOT/nwoas_scripts/uefi-s125/link_module.py" "$PAYLOAD" >> "$LOG" 2>&1
