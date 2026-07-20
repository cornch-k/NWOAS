#!/bin/bash
# NWOAS §3.2 diagnostic on the DEFERRED payload. Boots deferred-v1 + nwoas_capture_hook.py; at the
# kernel wedge the hook breaks in (hv.interrupt) and dumps FL1100 xHCI state READ-ONLY:
#   USBCMD(RS/INTE), USBSTS(HCH/HSE/EINT), DCBAAP + ERSTBA/ERDP (buffer placement: window<4GB vs
#   high>4GB), event-ring dequeue TRB, PORTSC(device connect/CCS), PCI CMD/BAR/PM/PCIe-err.
# This tells us WHY the kernel USB fails: buffer in high RAM (make-or-break) vs in-window-but-
# DART/coherence vs event-ring. Composition: load capture hook BEFORE pcie_emul-exp5.py so exp5's
# run_shell patch chains the hook's dump (per pcie_emul-exp5.py:216-221).
set -u
PAYLOAD=m1n1-payload-deferred-v1.bin
VDM=/Volumes/X31/NWOAS/macvdmtool/macvdmtool
LOG=/Volumes/X31/NWOAS/nwoas_scripts/logs/deferred-usbdiag.log
NTOS=/Volumes/X31/NWOAS/bw2tree/System32/ntoskrnl.exe
: > "$LOG"
echo "=== deferred §3.2 usbdiag start $(date '+%H:%M:%S') payload=$PAYLOAD ===" | tee -a "$LOG"
pkill -f run_guest 2>/dev/null; pkill -f "picocom.*debug-console" 2>/dev/null; sleep 1
sudo -n "$VDM" reboot serial >> "$LOG" 2>&1; sleep 10
[ -e /dev/cu.debug-console ] || { echo "no serial; abort" | tee -a "$LOG"; exit 1; }
cd /Volumes/X31/NWOAS/m1n1_windows
if [ -f "$NTOS" ]; then NTOSENV="NWOAS_NTOS=$NTOS"; else NTOSENV="NWOAS_NODUMMY=1"; fi
env M1N1DEVICE=/dev/cu.debug-console NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000 M1N1_KEEP_BAUD=1 NWOAS_LOG="$LOG" $NTOSENV \
  nohup .venv-hv/bin/python3 -u proxyclient/tools/run_guest.py \
  -r "$PAYLOAD" -m nwoas_capture_hook.py -m pcie_emul-exp5.py >> "$LOG" 2>&1 &
RGPID=$!
echo "run_guest pid=$RGPID (deferred + capture hook; hook dumps FL1100 at kernel wedge)" | tee -a "$LOG"
for i in $(seq 1 420); do   # ~35min: kernel reach + wedge + break-in + dump
  sleep 5
  kill -0 $RGPID 2>/dev/null || { echo "run_guest exited @iter$i $(date '+%H:%M:%S')" | tee -a "$LOG"; break; }
  if grep -qa "NWFL ===== dump end\|NWFL BAR0\|NWFL CMD=\|XHCI USBCMD=" "$LOG"; then
    echo "=== ★FL1100 DUMP CAPTURED @iter$i $(date '+%H:%M:%S') ===" | tee -a "$LOG"; sleep 10; break
  fi
  [ $((i % 12)) = 0 ] && echo "  [iter$i $(date '+%H:%M:%S')] reads=$(grep -ca UsbBootReadBlocks $LOG) hse=$(grep -ca 'usbsts=0x5' $LOG) kernel=$(grep -ca 'elr=0xfffff8' $LOG)" | tee -a "$LOG"
done
echo "=== usbdiag watcher done $(date '+%H:%M:%S'); run_guest pid=$RGPID ===" | tee -a "$LOG"
