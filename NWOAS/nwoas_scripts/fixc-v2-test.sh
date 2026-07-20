#!/bin/bash
# NWOAS §3.2 fix-C v2 (clean): apply apcie DART BYPASS ONCE + clear errors + free-run cursor
# window + final fault re-check. Does NOT kill the guest (keeps cursor window open). JUDGE=cursor.
set -u
ROOT=/Volumes/X31/NWOAS
PAYLOAD=m1n1-payload-iort-noleafdma-v1.bin
MODULE=pcie_emul-fixc-v2.py
VDM="$ROOT/macvdmtool/macvdmtool"
STAMP=$(date '+%H%M')
LOG="$ROOT/nwoas_scripts/logs/fixcv2-$STAMP.log"
: > "$LOG"
echo "=== fix-C v2 start $(date '+%H:%M:%S') ===" | tee -a "$LOG"
pkill -f run_guest 2>/dev/null; pkill -f "picocom.*debug-console" 2>/dev/null; sleep 1
PW="$(cat "$ROOT/.env")"
printf '%s\n' "$PW" | sudo -S -p '' "$VDM" reboot serial >> "$LOG" 2>&1
RC=$?; PW=""
[ $RC -ne 0 ] && echo "reboot serial failed rc=$RC" | tee -a "$LOG"
sleep 10
[ ! -e /dev/cu.debug-console ] && { echo "NO SERIAL; abort" | tee -a "$LOG"; exit 1; }
cd "$ROOT/m1n1_windows"
env M1N1DEVICE=/dev/cu.debug-console NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000 M1N1_KEEP_BAUD=1 NWOAS_LOG="$LOG" \
  nohup .venv-hv/bin/python3 -u proxyclient/tools/run_guest.py \
  -r "$PAYLOAD" -m "$MODULE" >> "$LOG" 2>&1 &
RGPID=$!
echo "run_guest pid=$RGPID (fix-C v2)" | tee -a "$LOG"
# watch only; DO NOT kill the guest (keep cursor window open)
for i in $(seq 1 780); do   # ~65min
  sleep 5
  kill -0 $RGPID 2>/dev/null || { echo "run_guest exited @iter$i $(date '+%H:%M:%S')" | tee -a "$LOG"; break; }
  if [ $((i % 6)) = 0 ]; then
    echo "  [iter$i $(date '+%H:%M:%S')] kernel=$(grep -ca 'elr=0xfffff8' $LOG) applied=$(grep -ca 'apply DART BYPASS ONCE' $LOG) verdict=$(grep -ca 'fcv2. FINAL VERDICT' $LOG)" | tee -a "$LOG"
  fi
done
echo "=== fix-C v2 watcher done $(date '+%H:%M:%S') ===" | tee -a "$LOG"
