#!/bin/bash
# NWOAS v10 (port re-enum fix) — does bootmgr get PAST the \BCD wedge? Run to bootmgfw load, then
# settle 4min to see if it proceeds (KEEP slot fires, re-init loop stops, boot progresses) or wedges.
set -u
LOG=/Volumes/X31/NWOAS/nwoas_scripts/logs/v10test.log
VDM=/Volumes/X31/NWOAS/macvdmtool/macvdmtool
: > "$LOG"
for attempt in 1 2 3 4 5; do
  echo "=== attempt $attempt start $(date '+%H:%M:%S') ===" | tee -a "$LOG"
  pkill -f run_guest 2>/dev/null; sleep 1
  sudo -n "$VDM" reboot serial >> "$LOG" 2>&1; sleep 10
  [ -e /dev/cu.debug-console ] || { echo "no serial" | tee -a "$LOG"; exit 1; }
  cd /Volumes/X31/NWOAS/m1n1_windows
  M1N1DEVICE=/dev/cu.debug-console NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000 M1N1_KEEP_BAUD=1 NWOAS_LOG="$LOG" \
    .venv-hv/bin/python3 -u proxyclient/tools/run_guest.py \
    -r m1n1-payload-exp5c-v10-portfix.bin -m pcie_emul-exp5.py >> "$LOG" 2>&1 &
  RGPID=$!
  echo "run_guest pid=$RGPID (attempt $attempt)" | tee -a "$LOG"
  base=$(grep -ca "Enable Slot Successfully" "$LOG"); dud=1; loaded=0
  for i in $(seq 1 400); do
    sleep 5
    kill -0 $RGPID 2>/dev/null || { echo "run_guest exited @iter$i" | tee -a "$LOG"; break; }
    if [ "$loaded" = 0 ] && grep -qa "EntryPoint=0x00010023490" "$LOG"; then
      loaded=$i; echo "=== bootmgfw LOADED @iter$i $(date '+%H:%M:%S'); settling 4min to observe ===" | tee -a "$LOG"
    fi
    # success hint: got PAST the BCD wedge (winload / more reads / kernel)
    if grep -qa "winload\|elr=0xfffff8\|Windows is loading" "$LOG"; then
      echo "=== ★PROGRESS PAST WEDGE @iter$i $(date '+%H:%M:%S') ===" | tee -a "$LOG"; sleep 10; dud=0; break
    fi
    if [ "$loaded" != 0 ] && [ $((i-loaded)) -ge 48 ]; then   # 4min after load
      echo "=== settled 4min post-load @iter$i $(date '+%H:%M:%S') ===" | tee -a "$LOG"; dud=0; break
    fi
    if [ "$i" -ge 108 ] && [ "$(grep -ca 'Enable Slot Successfully' "$LOG")" = "$base" ]; then
      echo "=== DUD, retry @iter$i ===" | tee -a "$LOG"; break
    fi
  done
  kill $RGPID 2>/dev/null; sleep 2; kill -9 $RGPID 2>/dev/null; pkill -f run_guest.py 2>/dev/null
  [ "$dud" = 0 ] && { echo "=== observed (attempt $attempt) ===" | tee -a "$LOG"; break; }
done
echo "=== ended $(date '+%H:%M:%S') ===" | tee -a "$LOG"
