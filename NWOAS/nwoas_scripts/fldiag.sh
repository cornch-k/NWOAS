#!/bin/bash
# NWOAS EXP-5 5c — FL1100 USBSTS-at-stall diagnostic run (kmutil-free).
# Replays exp5c-v7 (base=0, 4GB window) with nwoas_fl1100_diag.py added: at the ~201MB mid-read
# stall the diag breaks in and dumps the FL1100's OWN xHCI halt/error + event-ring state, to
# decide device-halt vs guest-spin. Single purposeful reboot for a clean proxy (no-hammer rule).
set -u
LOG=/Volumes/X31/NWOAS/nwoas_scripts/logs/fldiag-1.log
: > "$LOG"
pkill -f run_guest 2>/dev/null; pkill -f "picocom.*debug-console" 2>/dev/null; sleep 1
echo "=== NWOAS FL1100 diag start $(date '+%H:%M:%S') (SKIP_REBOOT=${SKIP_REBOOT:-0}) ===" | tee -a "$LOG"
if [ "${SKIP_REBOOT:-0}" != "1" ]; then
  sudo -n /Volumes/X31/NWOAS/macvdmtool/macvdmtool reboot serial >> "$LOG" 2>&1
  sleep 10
fi
[ -e /dev/cu.debug-console ] || { echo "=== no serial; abort ===" | tee -a "$LOG"; exit 1; }
pkill -f "picocom.*debug-console" 2>/dev/null || true
cd /Volumes/X31/NWOAS/m1n1_windows
M1N1DEVICE=/dev/cu.debug-console NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000 M1N1_KEEP_BAUD=1 NWOAS_LOG="$LOG" \
  .venv-hv/bin/python3 -u proxyclient/tools/run_guest.py \
  -r m1n1-payload-exp5c-v7.bin -m pcie_emul-exp5.py -m nwoas_fl1100_diag.py >> "$LOG" 2>&1 &
RGPID=$!
echo "=== run_guest pid=$RGPID; waiting for FLDIAG break-ins (240/330/420/540/720s) ===" | tee -a "$LOG"
# breaker's last break-in at 720s + burst/settle; run to ~830s then stop.
for i in $(seq 1 166); do   # 166*5s = ~830s
  sleep 5
  kill -0 $RGPID 2>/dev/null || { echo "=== run_guest exited @iter$i $(date '+%H:%M:%S') ===" | tee -a "$LOG"; break; }
  # early-out once all five break-ins have dumped
  [ "$(grep -ca 'NWOAS-FLDIAG BK#' "$LOG" 2>/dev/null)" -ge 15 ] && { echo "=== 5 break-ins captured @iter$i ===" | tee -a "$LOG"; sleep 3; break; }
done
kill $RGPID 2>/dev/null; sleep 2; kill -9 $RGPID 2>/dev/null; pkill -f run_guest.py 2>/dev/null
echo "=== FL1100 diag run ended $(date '+%H:%M:%S') ===" | tee -a "$LOG"
