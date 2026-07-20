#!/bin/bash
# NWOAS EXP-5 5c v8 (XhciDxe instrumented) — capture the NWOAS-PORT log to split the BCD-read
# transfer-completion failure 3 ways (halt / empty ring / unmatched). Auto-retry flaky boots.
set -u
LOG=/Volumes/X31/NWOAS/nwoas_scripts/logs/xhctest-v9.log
VDM=/Volumes/X31/NWOAS/macvdmtool/macvdmtool
: > "$LOG"
for attempt in 1 2 3 4 5; do
  echo "=== xhctest attempt $attempt start $(date '+%H:%M:%S') ===" | tee -a "$LOG"
  pkill -f run_guest 2>/dev/null; pkill -f "picocom.*debug-console" 2>/dev/null; sleep 1
  sudo -n "$VDM" reboot serial >> "$LOG" 2>&1
  sleep 10
  [ -e /dev/cu.debug-console ] || { echo "no serial; abort" | tee -a "$LOG"; exit 1; }
  pkill -f "picocom.*debug-console" 2>/dev/null || true
  cd /Volumes/X31/NWOAS/m1n1_windows
  M1N1DEVICE=/dev/cu.debug-console NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000 M1N1_KEEP_BAUD=1 NWOAS_LOG="$LOG" \
    .venv-hv/bin/python3 -u proxyclient/tools/run_guest.py \
    -r m1n1-payload-exp5c-v9-portdbg.bin -m pcie_emul-exp5.py >> "$LOG" 2>&1 &
  RGPID=$!
  echo "run_guest pid=$RGPID (attempt $attempt)" | tee -a "$LOG"
  base=$(grep -ca "Enable Slot Successfully" "$LOG"); dud=1
  for i in $(seq 1 300); do
    sleep 5
    kill -0 $RGPID 2>/dev/null || { echo "run_guest exited @iter$i" | tee -a "$LOG"; break; }
    if [ "$(grep -ca 'NWOAS-PORT' "$LOG")" -ge 3 ]; then
      echo "=== NWOAS-XHC CAPTURED (attempt $attempt) @iter$i $(date '+%H:%M:%S') ===" | tee -a "$LOG"; sleep 5; dud=0; break
    fi
    if [ "$i" -ge 108 ] && [ "$(grep -ca 'Enable Slot Successfully' "$LOG")" = "$base" ]; then
      echo "=== DUD, retry @iter$i ===" | tee -a "$LOG"; break
    fi
  done
  kill $RGPID 2>/dev/null; sleep 2; kill -9 $RGPID 2>/dev/null; pkill -f run_guest.py 2>/dev/null
  [ "$dud" = 0 ] && { echo "=== xhctest done (attempt $attempt) ===" | tee -a "$LOG"; break; }
done
echo "=== xhctest ended $(date '+%H:%M:%S') ===" | tee -a "$LOG"
