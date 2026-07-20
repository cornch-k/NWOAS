#!/bin/bash
# NWOAS §3.2: deferred-v1 + pcie_emul-exp5-fldiag.py (FL1100 kernel-phase register dump, no capture
# hook, no exp5 backing-fix breaker). At the kernel wedge the fldiag breaker breaks in and dumps the
# REAL FL1100 (0x6c0000000) state -> decides §3.2 root (addressing/HSE vs enumeration vs interrupt).
set -u
PAYLOAD=m1n1-payload-deferred-v1.bin
VDM=/Volumes/X31/NWOAS/macvdmtool/macvdmtool
LOG=/Volumes/X31/NWOAS/nwoas_scripts/logs/deferred-fldiag.log
: > "$LOG"
echo "=== deferred fldiag start $(date '+%H:%M:%S') payload=$PAYLOAD ===" | tee -a "$LOG"
pkill -f run_guest 2>/dev/null; pkill -f "picocom.*debug-console" 2>/dev/null; sleep 1
sudo -n "$VDM" reboot serial >> "$LOG" 2>&1; sleep 10
[ -e /dev/cu.debug-console ] || { echo "no serial; abort" | tee -a "$LOG"; exit 1; }
cd /Volumes/X31/NWOAS/m1n1_windows
env M1N1DEVICE=/dev/cu.debug-console NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000 M1N1_KEEP_BAUD=1 NWOAS_LOG="$LOG" \
  nohup .venv-hv/bin/python3 -u proxyclient/tools/run_guest.py \
  -r "$PAYLOAD" -m pcie_emul-exp5-fldiag.py >> "$LOG" 2>&1 &
RGPID=$!
echo "run_guest pid=$RGPID (deferred + FL1100 kernel dump)" | tee -a "$LOG"
for i in $(seq 1 540); do   # ~45min: kernel(~6min) + settle(60s) + 5 spaced dumps
  sleep 5
  kill -0 $RGPID 2>/dev/null || { echo "run_guest exited @iter$i $(date '+%H:%M:%S')" | tee -a "$LOG"; break; }
  if grep -qa "fldiag. breaker done\|fldiag. === dump #3 done\|fldiag. ★CMD.MEM=0" "$LOG"; then
    echo "=== ★FL1100 DUMPS DONE @iter$i $(date '+%H:%M:%S') ===" | tee -a "$LOG"; sleep 6; break
  fi
  [ $((i % 12)) = 0 ] && echo "  [iter$i $(date '+%H:%M:%S')] reads=$(grep -ca UsbBootReadBlocks $LOG) kernel=$(grep -ca 'elr=0xfffff8' $LOG) dumps=$(grep -ca 'fldiag. === DUMP' $LOG)" | tee -a "$LOG"
done
echo "=== fldiag watcher done $(date '+%H:%M:%S'); run_guest pid=$RGPID ===" | tee -a "$LOG"
