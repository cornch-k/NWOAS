#!/bin/bash
# S161: normalize UEFI RAM/backing layout; retain S163 50us HV and Windows RAM cap.
# Read-only CPU register snapshot is taken before UEFI; no DVFS change.
# PASS requires repeated checksum-correct I/O and an uninterrupted soak; build alone is insufficient.
set -eu
ROOT=/Volumes/X31/NWOAS
M1N1="$ROOT/m1n1_windows"
DEV=/dev/cu.usbmodemC07HL05SQ6NY1
VUART=/dev/cu.usbmodemC07HL05SQ6NY3
PY="$M1N1/.venv-hv/bin/python3"
HV="$ROOT/m1n1_windows-s159/build/m1n1-s163-eoi-gap50.bin"
PAYLOAD="$ROOT/nwoas_scripts/memory-s161/m1n1-payload-s161-8cpu-usba.bin"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/memory-s161-upload-p12-$(date '+%Y%m%d-%H%M%S').XXXXXX")

export NWOAS_HV_READ_TIMEOUT=30
export M1N1DEVICE="$DEV" M1N1_KEEP_BAUD=1 M1N1_SPLIT_CONSOLE=1 PYTHONDONTWRITEBYTECODE=1
export PYTHONPATH="$M1N1/proxyclient:$ROOT/nwoas_scripts"
export NWOAS_LINK_DIR="$LOG.link"
export NWOAS_CONTROLLER_DIR="$ROOT/nwoas_scripts/nvme-s139"
export NWOAS_BACKUP_INSTALL_SOURCE=1 NWOAS_CPU_PSTATE=12 NWOAS_LATE_P12=1
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
[ "$(shasum -a 256 "$PAYLOAD" | awk '{print $1}')" = c7b725851cdf3bbb1e192fcbcc8edac9642fb5ef1c0ffbe862ff39bd06d29271 ]
[ "$(shasum -a 256 "$ROOT/nwoas_scripts/target-query-s163/module.py" | awk '{print $1}')" = d415f9bcb0e1beaa72fd7dc65cd7debae97c65d2bfe59ac3bfe97393d164915b ]
[ "$(shasum -a 256 "$ROOT/nwoas_scripts/cpufreq-s164/capture.py" | awk '{print $1}')" = dbf4e9b20d8436b47d5b6a1ec1d4977a2606f5f35c7eadcf0587dab20c2a156e ]
[ "$(shasum -a 256 "$ROOT/nwoas_scripts/file-upload-s169/link_module.py" | awk '{print $1}')" = 56f17c9d28f299ecc1b59c57ec3fc87e9aa51cb0ac1df021893348e53c878c84 ]
[ "$(shasum -a 256 "$ROOT/nwoas_scripts/file-upload-s169/module.py" | awk '{print $1}')" = 83950f136c02c3dcf9467fbdf3e5718f3053621faf6703a14bbe2932c9b12b2b ]
[ "$(shasum -a 256 "$ROOT/nwoas_scripts/file-upload-s169/upload_adapter.py" | awk '{print $1}')" = 578ffd11a8f18fd7992ed5487b099803c8ba77dbd15dfeb2948ac87194300c43 ]
[ "$(shasum -a 256 "$ROOT/nwoas_scripts/file-upload-s169/receiver.py" | awk '{print $1}')" = 0834ddbf841fcd0c04ca79d602507ef38c00b1d95c8533ac277ff044427ab0cc ]
[ "$(shasum -a 256 "$ROOT/nwoas_scripts/file-upload-s169/client/NWPUT.EXE" | awk '{print $1}')" = 9789268cc68311776c8f80fe8f10e6503ccc56b3a5b902a40d8546c39383985b ]
[ "$(shasum -a 256 "$ROOT/nwoas_scripts/file-delivery-s168/client/NWOAS-S168.EXE" | awk '{print $1}')" = 1b40db539c222cd80c29cb03149e0f5d5a010848203914272eaee77434d895d9 ]
[ "$(shasum -a 256 "$ROOT/nwoas_scripts/cpufreq-s164/pstate12.py" | awk '{print $1}')" = caa2c29449b721d1000ac76e2c884387a75a079d381e7658e492817e7e3b25d2 ]
[ "$(shasum -a 256 "$ROOT/nwoas_scripts/cpufreq-s164/late_pstate12.py" | awk '{print $1}')" = ac6640e33f484963fba557b5684f8ba02de56dd63bdaddff931a69c58685a903 ]
if /usr/sbin/lsof -t "$DEV" "$VUART" >/dev/null 2>&1; then echo 'STOP: CDC pipe busy'; exit 1; fi
shasum -a 256 "$HV" "$PAYLOAD" "$ROOT/nwoas_scripts/cpufreq-s140/init.py" \
    "$ROOT/nwoas_scripts/nvme-s139/controller.py" >> "$LOG"
echo '[S161] normalized RAM/backing, S163 gap50us, late P12, unchanged Windows cap; S169 upload armed' | tee -a "$LOG"

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
    -m "$ROOT/nwoas_scripts/file-upload-s169/link_module.py" \
    -m "$ROOT/nwoas_scripts/file-upload-s169/module.py" \
    -m "$ROOT/nwoas_scripts/target-query-s163/module.py" \
    -m "$ROOT/nwoas_scripts/cpufreq-s164/late_pstate12.py" "$PAYLOAD" >> "$LOG" 2>&1
