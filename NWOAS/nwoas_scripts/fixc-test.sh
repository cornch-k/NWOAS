#!/bin/bash
# NWOAS §3.2 fix-C: apcie DART BYPASS at kernel stage so FL1100 reaches Windows' HIGH xHCI rings.
# no-kmutil (bridge already installed in outer m1n1). JUDGE = mouse cursor moves.
set -u
ROOT=/Volumes/X31/NWOAS
PAYLOAD=m1n1-payload-iort-noleafdma-v1.bin
MODULE=pcie_emul-fixc-dartbypass.py
VDM="$ROOT/macvdmtool/macvdmtool"
STAMP=$(date '+%H%M')
LOG="$ROOT/nwoas_scripts/logs/fixc-$STAMP.log"
: > "$LOG"
echo "=== fix-C DART-BYPASS start $(date '+%H:%M:%S') payload=$PAYLOAD module=$MODULE ===" | tee -a "$LOG"
pkill -f run_guest 2>/dev/null; pkill -f "picocom.*debug-console" 2>/dev/null; sleep 1
PW="$(cat "$ROOT/.env")"
printf '%s\n' "$PW" | sudo -S -p '' "$VDM" reboot serial >> "$LOG" 2>&1
RC=$?; PW=""
[ $RC -ne 0 ] && echo "macvdmtool reboot serial failed rc=$RC" | tee -a "$LOG"
sleep 10
[ ! -e /dev/cu.debug-console ] && { echo "NO SERIAL; abort" | tee -a "$LOG"; exit 1; }
cd "$ROOT/m1n1_windows"
env M1N1DEVICE=/dev/cu.debug-console NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000 M1N1_KEEP_BAUD=1 NWOAS_LOG="$LOG" \
  nohup .venv-hv/bin/python3 -u proxyclient/tools/run_guest.py \
  -r "$PAYLOAD" -m "$MODULE" >> "$LOG" 2>&1 &
RGPID=$!
echo "run_guest pid=$RGPID (fix-C)" | tee -a "$LOG"
for i in $(seq 1 660); do
  sleep 5
  kill -0 $RGPID 2>/dev/null || { echo "run_guest exited @iter$i $(date '+%H:%M:%S')" | tee -a "$LOG"; break; }
  if grep -qa "fixc. breaker done" "$LOG"; then
    echo "=== ★fix-C APPLIED @iter$i $(date '+%H:%M:%S') -- WATCH CURSOR ===" | tee -a "$LOG"
    # keep guest running for cursor observation; do not kill
  fi
  if [ $((i % 6)) = 0 ]; then
    echo "  [iter$i $(date '+%H:%M:%S')] kernel=$(grep -ca 'elr=0xfffff8' $LOG) applied=$(grep -ca 'fixc. === kernel stage' $LOG) tcr1100=$(grep -ca 'TCR\[0\]=0x1100' $LOG) validTRB=$(grep -ca 'NOW PRODUCING' $LOG)" | tee -a "$LOG"
  fi
done
echo "=== fix-C watcher done $(date '+%H:%M:%S') ===" | tee -a "$LOG"
