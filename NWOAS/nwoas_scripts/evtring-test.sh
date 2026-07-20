#!/bin/bash
# NWOAS §3.2 root-confirm: event-ring content diagnostic (evtring1) on the DCBAAP!=0 payload.
# Read-only, NO kmutil. Confirms WHY enumeration completions are never seen by the guest xHCI
# driver (the universal reset-storm livelock). no-hammer: pkill run_guest -> ONE reboot serial.
set -u
ROOT=/Volumes/X31/NWOAS
PAYLOAD=m1n1-payload-iort-noleafdma-v1.bin
MODULE=pcie_emul-exp5-evtring1.py
VDM="$ROOT/macvdmtool/macvdmtool"
STAMP=$(date '+%H%M')
LOG="$ROOT/nwoas_scripts/logs/evtring-$STAMP.log"
: > "$LOG"
echo "=== evtring §3.2 root-confirm start $(date '+%H:%M:%S') payload=$PAYLOAD module=$MODULE ===" | tee -a "$LOG"

pkill -f run_guest 2>/dev/null; pkill -f "picocom.*debug-console" 2>/dev/null; sleep 1
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
echo "run_guest pid=$RGPID (evtring1)" | tee -a "$LOG"
for i in $(seq 1 600); do
  sleep 5
  kill -0 $RGPID 2>/dev/null || { echo "run_guest exited @iter$i $(date '+%H:%M:%S')" | tee -a "$LOG"; break; }
  if grep -qa "evtring1. breaker done\|evtring1. === dump #8 done" "$LOG"; then
    echo "=== ★EVTRING DUMPS DONE @iter$i $(date '+%H:%M:%S') ===" | tee -a "$LOG"; sleep 6; break
  fi
  if [ $((i % 6)) = 0 ]; then
    echo "  [iter$i $(date '+%H:%M:%S')] kernel=$(grep -ca 'elr=0xfffff8' $LOG) dumps=$(grep -ca 'evtring1. === DUMP' $LOG)" | tee -a "$LOG"
  fi
done
echo "=== evtring watcher done $(date '+%H:%M:%S') ===" | tee -a "$LOG"
