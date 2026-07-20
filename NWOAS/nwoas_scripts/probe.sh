#!/bin/bash
# NWOAS EXP-5 5c — bootmgr busy-wait object probe (kmutil-free, auto-retry flaky boots).
# Each attempt: reboot -> run exp5c-v7 + pcie_emul-exp5 + nwoas_stall_probe. If the boot duds
# (no Enable Slot within ~9 min) reboot & retry (up to 4). On the deterministic stall the probe
# breaks in and dumps guest ctx + the busy-wait object (NWOAS-STALL BK#).
set -u
LOG=/Volumes/X31/NWOAS/nwoas_scripts/logs/probe-1.log
VDM=/Volumes/X31/NWOAS/macvdmtool/macvdmtool
: > "$LOG"
for attempt in 1 2 3 4; do
  echo "=== probe attempt $attempt start $(date '+%H:%M:%S') ===" | tee -a "$LOG"
  pkill -f run_guest 2>/dev/null; pkill -f "picocom.*debug-console" 2>/dev/null; sleep 1
  sudo -n "$VDM" reboot serial >> "$LOG" 2>&1
  sleep 10
  [ -e /dev/cu.debug-console ] || { echo "=== no serial; abort ===" | tee -a "$LOG"; exit 1; }
  pkill -f "picocom.*debug-console" 2>/dev/null || true
  cd /Volumes/X31/NWOAS/m1n1_windows
  M1N1DEVICE=/dev/cu.debug-console NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000 M1N1_KEEP_BAUD=1 NWOAS_LOG="$LOG" \
    .venv-hv/bin/python3 -u proxyclient/tools/run_guest.py \
    -r m1n1-payload-exp5c-v7.bin -m pcie_emul-exp5.py -m nwoas_stall_probe.py >> "$LOG" 2>&1 &
  RGPID=$!
  echo "=== run_guest pid=$RGPID (attempt $attempt) ===" | tee -a "$LOG"
  base=$(grep -ca "Enable Slot Successfully" "$LOG"); dud=1
  for i in $(seq 1 300); do   # up to ~25 min per attempt
    sleep 5
    kill -0 $RGPID 2>/dev/null || { echo "=== run_guest exited @iter$i ===" | tee -a "$LOG"; break; }
    if [ "$(grep -ca 'NWOAS-STALL BK#2' "$LOG")" -ge 1 ]; then
      echo "=== PROBE CAPTURED (attempt $attempt) @iter$i $(date '+%H:%M:%S') ===" | tee -a "$LOG"; sleep 3; dud=0; break
    fi
    # dud detect: ~9 min with no new reads -> reboot & retry
    if [ "$i" -ge 108 ] && [ "$(grep -ca 'Enable Slot Successfully' "$LOG")" = "$base" ]; then
      echo "=== DUD (no reads by 9min), retrying @iter$i $(date '+%H:%M:%S') ===" | tee -a "$LOG"; break
    fi
  done
  kill $RGPID 2>/dev/null; sleep 2; kill -9 $RGPID 2>/dev/null; pkill -f run_guest.py 2>/dev/null
  [ "$dud" = 0 ] && { echo "=== probe done (attempt $attempt) $(date '+%H:%M:%S') ===" | tee -a "$LOG"; break; }
done
echo "=== probe ended $(date '+%H:%M:%S') ===" | tee -a "$LOG"
