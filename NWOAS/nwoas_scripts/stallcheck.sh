#!/bin/bash
# NWOAS EXP-5 5c — stall determinism check (kmutil-free, unattended).
# Runs exp5c-v7 clean (no diag hook, natural timing) to the NATURAL mid-read stall, then records
# the stall metrics (max DMA IOVA, last UsbBootReadBlocks LBA, Map count) + the pc-ring (stuck
# loop). Compare to the 15:16 run (IOVA 0x43986124 / last LBA 0x13622 / maps 36278): a matching
# stall point => deterministic (data/format/corruption); a widely different point => flaky link.
set -u
LOG=/Volumes/X31/NWOAS/nwoas_scripts/logs/stallcheck-1.log
: > "$LOG"
pkill -f run_guest 2>/dev/null; pkill -f "picocom.*debug-console" 2>/dev/null; sleep 1
echo "=== NWOAS stallcheck start $(date '+%H:%M:%S') ===" | tee -a "$LOG"
sudo -n /Volumes/X31/NWOAS/macvdmtool/macvdmtool reboot serial >> "$LOG" 2>&1
sleep 10
[ -e /dev/cu.debug-console ] || { echo "=== no serial; abort ===" | tee -a "$LOG"; exit 1; }
pkill -f "picocom.*debug-console" 2>/dev/null || true
cd /Volumes/X31/NWOAS/m1n1_windows
M1N1DEVICE=/dev/cu.debug-console NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000 M1N1_KEEP_BAUD=1 NWOAS_LOG="$LOG" \
  .venv-hv/bin/python3 -u proxyclient/tools/run_guest.py \
  -r m1n1-payload-exp5c-v7.bin -m pcie_emul-exp5.py >> "$LOG" 2>&1 &
RGPID=$!
echo "=== run_guest pid=$RGPID; watching for natural stall (Maps frozen >90s after Enable Slot) ===" | tee -a "$LOG"
prev_maps=0; frozen=0; reads_seen=0
for i in $(seq 1 420); do   # 420*5s = ~35min cap
  sleep 5
  kill -0 $RGPID 2>/dev/null || { echo "=== run_guest exited @iter$i $(date '+%H:%M:%S') ===" | tee -a "$LOG"; break; }
  grep -qa "Enable Slot Successfully" "$LOG" && reads_seen=1
  maps=$(grep -ca "NWOAS MAP" "$LOG" 2>/dev/null)
  if [ "$reads_seen" = 1 ] && [ "$maps" = "$prev_maps" ] && [ "$maps" -gt 100 ]; then
    frozen=$((frozen+1))
  else
    frozen=0
  fi
  prev_maps=$maps
  if [ "$frozen" -ge 18 ]; then   # 18*5s = 90s no new Maps = stalled
    echo "=== STALL detected @iter$i $(date '+%H:%M:%S') maps=$maps (frozen 90s) ===" | tee -a "$LOG"
    sleep 30   # let pc-ring dump the stuck loop
    break
  fi
done
echo "=== stallcheck metrics $(date '+%H:%M:%S') ===" | tee -a "$LOG"
echo "maps=$(grep -ca 'NWOAS MAP' "$LOG")" | tee -a "$LOG"
kill $RGPID 2>/dev/null; sleep 2; kill -9 $RGPID 2>/dev/null; pkill -f run_guest.py 2>/dev/null
echo "=== stallcheck ended $(date '+%H:%M:%S') ===" | tee -a "$LOG"
