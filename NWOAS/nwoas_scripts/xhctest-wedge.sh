#!/bin/bash
# NWOAS v9 — capture NWOAS-PORT/NWOAS-XHC during the REAL bootmgfw BCD-read wedge (post-load),
# NOT during normal enumeration. Trigger on PMCC 0x100ad (bootmgfw telemetry = wedged) + settle.
set -u
LOG=/Volumes/X31/NWOAS/nwoas_scripts/logs/xhctest-wedge.log
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
    -r m1n1-payload-exp5c-v9-portdbg.bin -m pcie_emul-exp5.py >> "$LOG" 2>&1 &
  RGPID=$!
  echo "run_guest pid=$RGPID (attempt $attempt)" | tee -a "$LOG"
  base=$(grep -ca "Enable Slot Successfully" "$LOG"); dud=1
  for i in $(seq 1 340); do
    sleep 5
    kill -0 $RGPID 2>/dev/null || { echo "run_guest exited @iter$i" | tee -a "$LOG"; break; }
    # bootmgfw BCD wedge = PMCC 0x100ad accumulating. settle then capture.
    if [ "$(grep -ca 'NWOAS-PMCC elr=0x100ad' "$LOG")" -ge 20 ]; then
      echo "=== WEDGE (PMCC) @iter$i $(date '+%H:%M:%S'); settling 40s ===" | tee -a "$LOG"; sleep 40; dud=0; break
    fi
    if [ "$i" -ge 108 ] && [ "$(grep -ca 'Enable Slot Successfully' "$LOG")" = "$base" ]; then
      echo "=== DUD, retry @iter$i ===" | tee -a "$LOG"; break
    fi
  done
  kill $RGPID 2>/dev/null; sleep 2; kill -9 $RGPID 2>/dev/null; pkill -f run_guest.py 2>/dev/null
  [ "$dud" = 0 ] && { echo "=== wedge captured (attempt $attempt) ===" | tee -a "$LOG"; break; }
done
echo "=== ended $(date '+%H:%M:%S') ===" | tee -a "$LOG"
