#!/bin/bash
# NWOAS §6-A Path B: PYTHON-side event-ring dump on the INSTALLED v1 m1n1 (kmutil-FREE).
# v1 boots the guest to Windows reliably (kernel=354 confirmed); the Python module reads the ring
# with stage-2 translation (hv_pt_walk) + dc_ivac, via a few GENTLE kernel-gated break-ins (cap 4,
# vs the 284 that BSOD'd). No new hv build, no chainload, no kmutil.
# no-hammer: one reboot serial to get to the installed v1 proxy.
set -u
ROOT=/Volumes/X31/NWOAS
M1N1="$ROOT/m1n1_windows"
VDM="$ROOT/macvdmtool/macvdmtool"
DEV=/dev/cu.debug-console
PY="$M1N1/.venv-hv/bin/python3"
PAYLOAD=m1n1-payload-iort-noleafdma-v1.bin
MODULE=pcie_emul-exp5-dartprobe.py
STAMP=$(date '+%H%M')
LOG="$ROOT/nwoas_scripts/logs/dartprobe-$STAMP.log"
: > "$LOG"
echo "=== §6-A Path B (python, kmutil-free) start $(date '+%H:%M:%S') payload=$PAYLOAD module=$MODULE ===" | tee -a "$LOG"

pkill -f run_guest 2>/dev/null; pkill -f "picocom.*debug-console" 2>/dev/null; sleep 1
PW="$(cat "$ROOT/.env")"
printf '%s\n' "$PW" | sudo -S -p '' "$VDM" reboot serial >> "$LOG" 2>&1
PW=""
sleep 10
if [ ! -e "$DEV" ]; then echo "NO SERIAL $DEV; abort" | tee -a "$LOG"; exit 1; fi
pkill -f "picocom.*debug-console" 2>/dev/null || true

cd "$M1N1"
env M1N1DEVICE="$DEV" NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000 M1N1_KEEP_BAUD=1 NWOAS_LOG="$LOG" \
  PYTHONPATH="$M1N1/proxyclient" nohup "$PY" -u proxyclient/tools/run_guest.py \
  -r "$PAYLOAD" -m "$MODULE" >> "$LOG" 2>&1 &
RGPID=$!
echo "run_guest pid=$RGPID (Path B, gentle break-ins)" | tee -a "$LOG"

for i in $(seq 1 600); do
  sleep 5
  kill -0 $RGPID 2>/dev/null || { echo "run_guest exited @iter$i $(date '+%H:%M:%S')" | tee -a "$LOG"; break; }
  if grep -qa "breaker done" "$LOG"; then
    echo "=== ★breaker done @iter$i $(date '+%H:%M:%S') ===" | tee -a "$LOG"; sleep 6; break
  fi
  if [ $((i % 12)) = 0 ]; then
    echo "  [iter$i $(date '+%H:%M:%S')] kernel=$(grep -ca 'elr=0xfffff8' $LOG) stages=$(grep -ca "BREAK-IN" $LOG)" | tee -a "$LOG"
  fi
done
echo "=== Path B watcher done $(date '+%H:%M:%S') ===" | tee -a "$LOG"
echo "--- [dartp] lines ---" | tee -a "$LOG"
grep -a '\[dartp\]' "$LOG" | tail -80 | tee -a "$LOG"
