#!/bin/bash
# NWOAS host-side wait-object capture (no kmutil): reboot mini -> boot guest with capture hook
# -> at the wedge, break in (hv.interrupt) -> READ-ONLY scheduler walk prints wait object.
# Uses currently-installed device m1n1 (wfidiag11); the walk is host-side via hv.readmem.
LOG=/Volumes/X31/NWOAS/nwoas_scripts/logs/capture-walk.log
: > "$LOG"
pkill -f run_guest 2>/dev/null; pkill -f "picocom.*debug-console" 2>/dev/null; sleep 1
echo "=== NWOAS host-side wait-object capture start $(date '+%H:%M:%S') ===" >> "$LOG"
sudo -n /Volumes/X31/NWOAS/macvdmtool/macvdmtool reboot serial >> "$LOG" 2>&1
sleep 10
[ -e /dev/cu.debug-console ] || { echo "=== capture done (no serial) ===" >> "$LOG"; exit 1; }
pkill -f "picocom.*debug-console" 2>/dev/null || true
cd /Volumes/X31/NWOAS/m1n1_windows
M1N1DEVICE=/dev/cu.debug-console M1N1_KEEP_BAUD=1 NWOAS_LOG="$LOG" NWOAS_NTOS=/Volumes/X31/NWOAS/bw2tree/System32/ntoskrnl.exe .venv-hv/bin/python3 \
  proxyclient/tools/run_guest.py -r m1n1-payload.bin -m pcie_emul.py -m nwoas_capture_hook.py >> "$LOG" 2>&1 &
RGPID=$!
# Run long enough for: kernel reach (~15min) + wedge + break-in (<=23min) + walk (~1min) + margin.
for i in $(seq 1 400); do   # 400*5s = ~33min (wedge ~18min + break-in + walk + proc-walk)
  sleep 5
  grep -qa "NWPROC-T ===== proc_walk done" "$LOG" && { echo "=== capture PROC-WALK DONE @iter$i $(date '+%H:%M:%S') ===" >> "$LOG"; sleep 5; break; }
  kill -0 $RGPID 2>/dev/null || { echo "=== rg-exit@iter$i ===" >> "$LOG"; break; }
done
kill $RGPID 2>/dev/null; sleep 2; kill -9 $RGPID 2>/dev/null; pkill -f run_guest.py 2>/dev/null
echo "=== capture run ended $(date '+%H:%M:%S') ===" >> "$LOG"
