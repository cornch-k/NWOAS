#!/bin/bash
# S170: ReadyToBoot P12; S163 50us HV and original S139 memory/USB-A layout.
# Host leaves P7; firmware applies P12 after inner m1n1 init.
# PASS requires repeated checksum-correct I/O and an uninterrupted soak; build alone is insufficient.
set -eu
ROOT=/Volumes/X31/NWOAS
M1N1="$ROOT/m1n1_windows"
DEV=/dev/cu.usbmodemC07HL05SQ6NY1
VUART=/dev/cu.usbmodemC07HL05SQ6NY3
PY="$M1N1/.venv-hv/bin/python3"
HV="$ROOT/m1n1_windows-s159/build/m1n1-s163-eoi-gap50.bin"
PAYLOAD="$ROOT/nwoas_scripts/cpufreq-s170/m1n1-payload-s170-p12-usba.bin"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/cpufreq-s170-$(date '+%Y%m%d-%H%M%S').XXXXXX")

export NWOAS_HV_READ_TIMEOUT=30
export M1N1DEVICE="$DEV" M1N1_KEEP_BAUD=1 M1N1_SPLIT_CONSOLE=1 PYTHONDONTWRITEBYTECODE=1
export PYTHONPATH="$M1N1/proxyclient:$ROOT/nwoas_scripts"
export NWOAS_LINK_DIR="$LOG.link"
export NWOAS_CONTROLLER_DIR="$ROOT/nwoas_scripts/nvme-s139"
export NWOAS_CPU_PSTATE=7
export NWOAS_MAX_TRANSFER=1048576 NWOAS_NVME_FAST_MASK=1 NWOAS_TARGET_QUERY=1
export NWOAS_EXCLUDE_WINDOW=1 NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000 NWOAS_LOG="$LOG"
export NWOAS_DCP_LOG="$LOG.dcp" NWOAS_HV_IMAGE="$HV"

echo "Log: $LOG"
[ -e "$DEV" ] || { echo 'STOP: direct CDC proxy missing'; exit 1; }
[ -e "$VUART" ] || { echo 'STOP: direct CDC diagnostic pipe missing'; exit 1; }
[ "$(stat -f %z "$HV")" -eq 2162688 ]
[ "$(shasum -a 256 "$HV" | awk '{print $1}')" = bd8f16f286c8d1141df4a17d9b85eafdd4c2d39d5680c6ed75b63429f061d166 ]
[ "$(shasum -a 256 "$ROOT/nwoas_scripts/nvme-s160/guest_module.py" | awk '{print $1}')" = 5f8cf61b3ccc32fc53a40f3ea14aa3ffa5ffbd34f672e1e7092e6eb291db3c05 ]
[ "$(stat -f %z "$PAYLOAD")" -eq 32342016 ]
[ "$(shasum -a 256 "$PAYLOAD" | awk '{print $1}')" = 19aa9dcf50ce257eee860031c22f36208140877bd13fd602a1869aad778c6e3f ]
[ "$(shasum -a 256 "$ROOT/nwoas_scripts/target-query-s163/module.py" | awk '{print $1}')" = d415f9bcb0e1beaa72fd7dc65cd7debae97c65d2bfe59ac3bfe97393d164915b ]
[ "$(shasum -a 256 "$ROOT/nwoas_scripts/cpufreq-s164/capture.py" | awk '{print $1}')" = dbf4e9b20d8436b47d5b6a1ec1d4977a2606f5f35c7eadcf0587dab20c2a156e ]
if /usr/sbin/lsof -t "$DEV" "$VUART" >/dev/null 2>&1; then echo 'STOP: CDC pipe busy'; exit 1; fi
shasum -a 256 "$HV" "$PAYLOAD" "$ROOT/nwoas_scripts/cpufreq-s140/init.py" \
    "$ROOT/nwoas_scripts/nvme-s139/controller.py" >> "$LOG"
echo '[S170] ReadyToBoot P12 candidate; host P7 unchanged; S163 gap50us, S139 memory, 8 cores USB-A' | tee -a "$LOG"

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

"$PY" -u "$M1N1/proxyclient/tools/run_guest.py" -r --strict-init \
    --pre-init-script "$M1N1/tahoe_dcp_guest_hook.py" \
    -m "$ROOT/nwoas_scripts/cpufreq-s140/init.py" \
    -m "$ROOT/nwoas_scripts/cpufreq-s164/capture.py" \
    -m "$ROOT/nwoas_scripts/rescue-s138/pcie_usba_only.py" \
    -m "$ROOT/nwoas_scripts/smp-s133/pmgr_gate.py" \
    -m "$ROOT/nwoas_scripts/nvme-s160/guest_module.py" \
    -m "$ROOT/nwoas_scripts/uefi-s125/link_module.py" \
    -m "$ROOT/nwoas_scripts/target-query-s163/module.py" "$PAYLOAD" >> "$LOG" 2>&1
