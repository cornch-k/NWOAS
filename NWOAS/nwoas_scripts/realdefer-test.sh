#!/bin/bash
# NWOAS §3.2 "real deferred map" experiment: realdefer-v1 (GetMemoryMap wrapper hides high RAM
# from Windows only) + pcie_emul-exp5-fldiag4.py. Reboots mini into serial (macvdmtool, sudo pw
# from .env, NEVER echoed), boots to the Windows kernel, dumps REAL FL1100 (0x6c0000000) state.
# PASS = kernel-stage DCBAAP != 0 at a <4GB addr AND USBCMD.RS=1 (else §3.2 still unresolved).
# no-hammer: pkill stale run_guest -> ONE reboot serial (fresh m1n1) -> run_guest new payload.
set -u
ROOT=/Volumes/X31/NWOAS
PAYLOAD=m1n1-payload-realdefer-v1.bin
MODULE=pcie_emul-exp5-fldiag4.py
VDM="$ROOT/macvdmtool/macvdmtool"
STAMP=$(date '+%H%M')
LOG="$ROOT/nwoas_scripts/logs/realdefer-fldiag-$STAMP.log"
: > "$LOG"
echo "=== realdefer §3.2 test start $(date '+%H:%M:%S') payload=$PAYLOAD module=$MODULE ===" | tee -a "$LOG"

# --- kill stale run_guest, then ONE reboot into serial (sudo pw from .env, kept out of logs/argv) ---
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
echo "run_guest pid=$RGPID (realdefer + fldiag4)" | tee -a "$LOG"
for i in $(seq 1 600); do   # ~50min budget
  sleep 5
  kill -0 $RGPID 2>/dev/null || { echo "run_guest exited @iter$i $(date '+%H:%M:%S')" | tee -a "$LOG"; break; }
  if grep -qa "fldiag4. breaker done\|fldiag4. === dump #4 done\|fldiag4. ★CMD.MEM=0\|fldiag4. === DUMP #4" "$LOG"; then
    echo "=== ★FL1100 DUMPS DONE @iter$i $(date '+%H:%M:%S') ===" | tee -a "$LOG"; sleep 6; break
  fi
  if [ $((i % 6)) = 0 ]; then
    echo "  [iter$i $(date '+%H:%M:%S')] hook=$(grep -ca 'HideHighRam ReadyToBoot hook registered' $LOG) installed=$(grep -ca 'HIDE(wrap) installed' $LOG) fill1=$(grep -ca 'HIDE(wrap) fill#1' $LOG) reads=$(grep -ca UsbBootReadBlocks $LOG) kernel=$(grep -ca 'elr=0xfffff8' $LOG) dumps=$(grep -ca 'fldiag4. === DUMP' $LOG)" | tee -a "$LOG"
  fi
done
echo "=== realdefer watcher done $(date '+%H:%M:%S'); run_guest pid=$RGPID ===" | tee -a "$LOG"
echo "--- KEY SIGNALS ---" | tee -a "$LOG"
grep -a "HIDE begin\|HIDE(wrap) installed\|HIDE(wrap) fill#1\|abort_guard" "$LOG" | tail -6 | tee -a "$LOG"
grep -a "DCBAAP\|USBCMD=\|VERDICT" "$LOG" | tail -20 | tee -a "$LOG"
