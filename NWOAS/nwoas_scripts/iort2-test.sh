#!/bin/bash
# NWOAS §3.2 Model-2 experiment: iort-v1 (adds IORT Named Component for \_SB.SCB0.XHC0, the
# single structural element RPi4 ships that we omitted) + pcie_emul-exp5-fldiag4.py. Wrapper is
# INERT (deferred baseline) so IORT is the ONLY variable. Reboots mini into serial (macvdmtool,
# sudo pw from .env, NEVER echoed), boots to Windows kernel, dumps REAL FL1100 (0x6c0000000).
# HYPOTHESIS: IORT gives Windows ARM64 the DMA-master metadata to build a functional DMA adapter
# -> AllocateCommonBuffer(DCBAA) succeeds -> DCBAAP != 0. PASS = kernel-stage DCBAAP != 0 (<4GB)
# AND USBCMD.RS=1 (real success = mouse cursor). no-hammer: pkill run_guest -> ONE reboot serial.
set -u
ROOT=/Volumes/X31/NWOAS
PAYLOAD=m1n1-payload-iort-noleafdma-v1.bin
MODULE=pcie_emul-exp5-fldiag4.py
VDM="$ROOT/macvdmtool/macvdmtool"
STAMP=$(date '+%H%M')
LOG="$ROOT/nwoas_scripts/logs/iort-fldiag-$STAMP.log"
: > "$LOG"
echo "=== iort §3.2 Model-2 test start $(date '+%H:%M:%S') payload=$PAYLOAD module=$MODULE ===" | tee -a "$LOG"

pkill -f run_guest 2>/dev/null; pkill -f "picocom.*debug-console" 2>/dev/null; sleep 1
set +x
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
echo "run_guest pid=$RGPID (iort + fldiag4)" | tee -a "$LOG"
for i in $(seq 1 600); do   # ~50min budget
  sleep 5
  kill -0 $RGPID 2>/dev/null || { echo "run_guest exited @iter$i $(date '+%H:%M:%S')" | tee -a "$LOG"; break; }
  if grep -qa "fldiag4. breaker done\|fldiag4. === dump #4 done\|fldiag4. ★CMD.MEM=0\|fldiag4. === DUMP #4" "$LOG"; then
    echo "=== ★FL1100 DUMPS DONE @iter$i $(date '+%H:%M:%S') ===" | tee -a "$LOG"; sleep 6; break
  fi
  if [ $((i % 6)) = 0 ]; then
    echo "  [iter$i $(date '+%H:%M:%S')] iort_inst=$(grep -cae 'IORT\|Installing ACPI\|InstallAcpiTable' $LOG) reads=$(grep -ca UsbBootReadBlocks $LOG) kernel=$(grep -ca 'elr=0xfffff8' $LOG) dumps=$(grep -ca 'fldiag4. === DUMP' $LOG)" | tee -a "$LOG"
  fi
done
echo "=== iort watcher done $(date '+%H:%M:%S'); run_guest pid=$RGPID ===" | tee -a "$LOG"
echo "--- KEY SIGNALS ---" | tee -a "$LOG"
grep -a "DCBAAP\|USBCMD=\|PORTSC\[0\]\|VERDICT" "$LOG" | tail -20 | tee -a "$LOG"
