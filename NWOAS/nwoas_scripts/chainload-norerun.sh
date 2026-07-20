#!/bin/bash
# NWOAS kmutil-free hv run, NO reboot (use when the mini is ALREADY at a healthy
# m1n1 proxy — e.g. right after a controlled recovery reboot + verified nop).
# Avoids hammering macvdmtool reboot. chainload the FRESH hv -> run_guest.
#
# The upload (writemem) is single-shot; a VDM byte-drop aborts it (ST_XFRERR).
# So retry the whole chainload up to 3x (no reboot between — just re-upload).
set -u
ROOT=/Volumes/X31/NWOAS
M1N1="$ROOT/m1n1_windows"
DEV=/dev/cu.debug-console
PY="$M1N1/.venv-hv/bin/python3"
HV_FRESH="$M1N1/build/m1n1.bin"
PAYLOAD="${NWOAS_PAYLOAD:-$M1N1/m1n1-payload-iort-noleafdma-v1.bin}"
MODULE="${NWOAS_MODULE:-$M1N1/pcie_emul-exp5-evtdump.py}"
STAMP=$(date '+%H%M')
LOG="$ROOT/nwoas_scripts/logs/chainload-norerun-$STAMP.log"
: > "$LOG"
echo "=== kmutil-free NO-REBOOT run $(date '+%H:%M:%S') fresh-hv=$HV_FRESH ===" | tee -a "$LOG"
[ -e "$DEV" ] || { echo "NO SERIAL $DEV" | tee -a "$LOG"; exit 1; }
pkill -f run_guest 2>/dev/null; sleep 1

CL_OK=0
for try in 1 2 3; do
  echo "=== [chainload attempt $try] $(date '+%H:%M:%S') ===" | tee -a "$LOG"
  M1N1DEVICE="$DEV" env M1N1_KEEP_BAUD=1 PYTHONPATH="$M1N1/proxyclient" "$PY" -u \
    "$M1N1/proxyclient/tools/chainload.py" -r "$HV_FRESH" >> "$LOG" 2>&1
  if grep -qa "Proxy is alive again\|WARNING: chainload confirm" "$LOG"; then
    echo "  chainload OK (fresh hv jumped) attempt $try" | tee -a "$LOG"; CL_OK=1; break
  fi
  echo "  chainload attempt $try failed (VDM drop?), retrying (no reboot)..." | tee -a "$LOG"
  sleep 2
done
if [ "$CL_OK" != 1 ]; then
  echo "=== chainload failed after 3 tries — NOT rebooting (no-hammer). Fall back to kmutil. ===" | tee -a "$LOG"
  exit 2
fi
sleep 2

echo "=== [run_guest] guest under freshly-chainloaded hv $(date '+%H:%M:%S') ===" | tee -a "$LOG"
M1N1DEVICE="$DEV" env NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000 M1N1_KEEP_BAUD=1 NWOAS_LOG="$LOG" \
  PYTHONPATH="$M1N1/proxyclient" nohup "$PY" -u "$M1N1/proxyclient/tools/run_guest.py" \
  -r "$PAYLOAD" -m "$MODULE" >> "$LOG" 2>&1 &
RGPID=$!
echo "run_guest pid=$RGPID" | tee -a "$LOG"

for i in $(seq 1 540); do
  sleep 5
  kill -0 $RGPID 2>/dev/null || { echo "run_guest exited @iter$i $(date '+%H:%M:%S')" | tee -a "$LOG"; break; }
  DUMPS=$(grep -ca 'HVLOG: EVTDUMP erstba' "$LOG")
  if [ "$DUMPS" -ge 16 ]; then
    echo "=== ★16 EVTDUMPS DONE @iter$i $(date '+%H:%M:%S') ===" | tee -a "$LOG"; sleep 6; break
  fi
  if [ $((i % 12)) = 0 ]; then
    echo "  [iter$i $(date '+%H:%M:%S')] kernel=$(grep -ca 'elr=0xfffff8' $LOG) FLC=$(grep -ca 'NWOAS-FLC' $LOG) EVTDUMP=$DUMPS" | tee -a "$LOG"
  fi
done
echo "=== norerun watcher done $(date '+%H:%M:%S') ===" | tee -a "$LOG"
echo "--- EVTDUMP summary ---" | tee -a "$LOG"
grep -a 'HVLOG: EVTDUMP' "$LOG" | tail -60 | tee -a "$LOG"
