#!/bin/bash
# NWOAS §6-A: C-side (EL2) event-ring dump — decisive reachability-vs-coherence probe.
#
# PREREQUISITE (one-time, USER + physical 1TR): the NEW outer m1n1 with the nwoas_dump_evtring
# instrumentation (m1n1_windows/build/m1n1.bin, built 2026-07-12) must be kmutil-installed as the
# EL2 hypervisor. This harness only does `reboot serial` + run_guest; it does NOT install anything.
# The dump lines ("HVLOG: EVTDUMP ...") are emitted by that outer m1n1, not by the Python module.
#
# The module (pcie_emul-exp5-evtdump.py) is CLEAN: it only reproduces the ring-at-high-RAM
# environment and lets the guest run free. NO break-in (284 break-ins BSOD'd last time), NO Python
# high-RAM reads (poison). All ring reading is EL2 C-side. no-hammer: pkill run_guest -> ONE reboot.
set -u
ROOT=/Volumes/X31/NWOAS
PAYLOAD=m1n1-payload-iort-noleafdma-v1.bin
MODULE=pcie_emul-exp5-evtdump.py
VDM="$ROOT/macvdmtool/macvdmtool"
STAMP=$(date '+%H%M')
LOG="$ROOT/nwoas_scripts/logs/evtdump-$STAMP.log"
: > "$LOG"
echo "=== §6-A EL2 event-ring dump start $(date '+%H:%M:%S') payload=$PAYLOAD module=$MODULE ===" | tee -a "$LOG"
echo "=== (requires NEW build/m1n1.bin kmutil-installed as outer hv) ===" | tee -a "$LOG"

pkill -f run_guest 2>/dev/null; pkill -f "picocom.*debug-console" 2>/dev/null; sleep 1
PW="$(cat "$ROOT/.env")"
printf '%s\n' "$PW" | sudo -S -p '' "$VDM" reboot serial >> "$LOG" 2>&1
RC=$?
PW=""
if [ $RC -ne 0 ]; then echo "macvdmtool reboot serial failed rc=$RC (cable? sudo?)" | tee -a "$LOG"; fi
sleep 10
if [ ! -e /dev/cu.debug-console ]; then echo "NO SERIAL /dev/cu.debug-console; abort (cable loose?)" | tee -a "$LOG"; exit 1; fi

cd "$ROOT/m1n1_windows"
env M1N1DEVICE=/dev/cu.debug-console NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000 M1N1_KEEP_BAUD=1 NWOAS_LOG="$LOG" \
  nohup .venv-hv/bin/python3 -u proxyclient/tools/run_guest.py \
  -r "$PAYLOAD" -m "$MODULE" >> "$LOG" 2>&1 &
RGPID=$!
echo "run_guest pid=$RGPID (evtdump, no break-in)" | tee -a "$LOG"

# Boot to Windows kernel takes ~15-25 min at 115200. Then the USBSTS livelock drives the EL2 dumps
# (cap 16). Watch up to ~45 min. Report kernel-reached, EVTDUMP count, and latch of ERSTBA.
for i in $(seq 1 540); do
  sleep 5
  kill -0 $RGPID 2>/dev/null || { echo "run_guest exited @iter$i $(date '+%H:%M:%S')" | tee -a "$LOG"; break; }
  DUMPS=$(grep -ca 'HVLOG: EVTDUMP erstba' "$LOG")
  if [ "$DUMPS" -ge 16 ]; then
    echo "=== ★16 EVTDUMPS DONE @iter$i $(date '+%H:%M:%S') — decisive data captured ===" | tee -a "$LOG"; sleep 6; break
  fi
  if [ $((i % 12)) = 0 ]; then
    echo "  [iter$i $(date '+%H:%M:%S')] kernel=$(grep -ca 'elr=0xfffff8' $LOG) FLC=$(grep -ca 'NWOAS-FLC' $LOG) EVTDUMP=$DUMPS erst-fault=$(grep -ca 'EVTDUMP erst-fault' $LOG)" | tee -a "$LOG"
  fi
done
echo "=== evtdump watcher done $(date '+%H:%M:%S') ===" | tee -a "$LOG"
echo "--- EVTDUMP summary ---" | tee -a "$LOG"
grep -a 'HVLOG: EVTDUMP' "$LOG" | tail -60 | tee -a "$LOG"
