#!/bin/bash
# NWOAS §3.2 baseline: deferred-v1 + pcie_emul-exp5-fldiag2.py (adds AC64 + dwc3 GCTL reads).
# Reboots mini into serial via macvdmtool (sudo password from .env, NEVER echoed), boots to the
# Windows kernel, then dumps the REAL FL1100 (0x6c0000000) state 5x. Purpose: reproduce DCBAAP=0,
# capture HCCPARAMS1.AC64 (candidate-B viability) and dwc3 GCTL mode (Track2). Diagnostic only.
set -u
ROOT=/Volumes/X31/NWOAS
PAYLOAD=m1n1-payload-deferred-v1.bin
MODULE=pcie_emul-exp5-fldiag2.py
VDM="$ROOT/macvdmtool/macvdmtool"
LOG="$ROOT/nwoas_scripts/logs/deferred-fldiag2.log"
: > "$LOG"
echo "=== deferred fldiag2 baseline start $(date '+%H:%M:%S') payload=$PAYLOAD module=$MODULE ===" | tee -a "$LOG"

# --- reboot into serial (sudo password from .env, kept out of logs/argv) ---
pkill -f run_guest 2>/dev/null; pkill -f "picocom.*debug-console" 2>/dev/null; sleep 1
set +x
PW="$(cat "$ROOT/.env")"
printf '%s\n' "$PW" | sudo -S -p '' "$VDM" reboot serial >> "$LOG" 2>&1
RC=$?
PW=""
if [ $RC -ne 0 ]; then echo "macvdmtool reboot serial failed rc=$RC (cable? sudo?)" | tee -a "$LOG"; fi
sleep 10
if [ ! -e /dev/cu.debug-console ]; then echo "NO SERIAL /dev/cu.debug-console; abort (cable loose?)" | tee -a "$LOG"; exit 1; fi

cd "$ROOT/m1n1_windows"
env M1N1DEVICE=/dev/cu.debug-console NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000 M1N1_KEEP_BAUD=1 NWOAS_LOG="$LOG" \
  nohup .venv-hv/bin/python3 -u proxyclient/tools/run_guest.py \
  -r "$PAYLOAD" -m "$MODULE" >> "$LOG" 2>&1 &
RGPID=$!
echo "run_guest pid=$RGPID (deferred baseline + fldiag2)" | tee -a "$LOG"
for i in $(seq 1 540); do   # ~45min budget
  sleep 5
  kill -0 $RGPID 2>/dev/null || { echo "run_guest exited @iter$i $(date '+%H:%M:%S')" | tee -a "$LOG"; break; }
  if grep -qa "fldiag2. breaker done\|fldiag2. === dump #3 done\|fldiag2. ★CMD.MEM=0" "$LOG"; then
    echo "=== ★FL1100 DUMPS DONE @iter$i $(date '+%H:%M:%S') ===" | tee -a "$LOG"; sleep 6; break
  fi
  [ $((i % 12)) = 0 ] && echo "  [iter$i $(date '+%H:%M:%S')] reads=$(grep -ca UsbBootReadBlocks $LOG) kernel=$(grep -ca 'elr=0xfffff8' $LOG) dumps=$(grep -ca 'fldiag2. === DUMP' $LOG)" | tee -a "$LOG"
done
echo "=== fldiag2 watcher done $(date '+%H:%M:%S'); run_guest pid=$RGPID ===" | tee -a "$LOG"
