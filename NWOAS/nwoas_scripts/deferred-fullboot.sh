#!/bin/bash
# NWOAS deferred FULL boot to Windows (NO premature kill).
# deferred-v1 already confirmed past the UEFI read stall (reads 9966 vs old 2304, hse 0) and reached
# the Windows kernel (elr=0xfffff801, PSCI SMC). This run lets it boot all the way so the user can:
#   (1) see Windows Setup render on screen, (2) test Piece B = Windows kernel USB / §3.2
#       (mouse/keyboard move? WOA disk appears in diskpart?) -- the new thing deferred adds vs no-EXP-5.
# run_guest is LEFT ALIVE after the kernel is reached so the user can interact. Do not kill until done.
set -u
PAYLOAD=m1n1-payload-deferred-v1.bin
VDM=/Volumes/X31/NWOAS/macvdmtool/macvdmtool
LOG=/Volumes/X31/NWOAS/nwoas_scripts/logs/deferred-fullboot.log
: > "$LOG"
echo "=== deferred fullboot start $(date '+%H:%M:%S') payload=$PAYLOAD ===" | tee -a "$LOG"
pkill -f run_guest 2>/dev/null; sleep 1
sudo -n "$VDM" reboot serial >> "$LOG" 2>&1; sleep 10
[ -e /dev/cu.debug-console ] || { echo "no serial; abort" | tee -a "$LOG"; exit 1; }
cd /Volumes/X31/NWOAS/m1n1_windows
M1N1DEVICE=/dev/cu.debug-console NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000 M1N1_KEEP_BAUD=1 NWOAS_LOG="$LOG" \
  nohup .venv-hv/bin/python3 -u proxyclient/tools/run_guest.py \
  -r "$PAYLOAD" -m pcie_emul-exp5.py >> "$LOG" 2>&1 &
RGPID=$!
echo "run_guest pid=$RGPID (LEFT ALIVE for full boot + USB test)" | tee -a "$LOG"
kernel=0
for i in $(seq 1 220); do   # ~18min watch; run_guest keeps running after
  sleep 5
  kill -0 $RGPID 2>/dev/null || { echo "run_guest EXITED @iter$i $(date '+%H:%M:%S') (flaky _start? re-run)" | tee -a "$LOG"; break; }
  reads=$(grep -ca UsbBootReadBlocks "$LOG"); hse=$(grep -ca 'usbsts=0x5' "$LOG")
  if [ "$kernel" = 0 ] && grep -qa 'Windows is loading\|elr=0xfffff8\|NWOAS-PMCC\|PSCI SMC' "$LOG"; then
    kernel=$i
    echo "=== KERNEL REACHED @iter$i reads=$reads hse=$hse $(date '+%H:%M:%S'); run_guest LEFT ALIVE for screen/USB test ===" | tee -a "$LOG"
  fi
  # NWFL/xHCI kernel-side USB markers (Piece B): DCBAAP set, RS=1, disk enum
  if grep -qa 'NWFL\|DCBAAP\|diskpart\|WOA' "$LOG"; then
    echo "=== ★USB/DISK marker @iter$i $(date '+%H:%M:%S') ===" | tee -a "$LOG"
  fi
  if [ $((i % 6)) = 0 ]; then echo "  [iter$i $(date '+%H:%M:%S')] reads=$reads hse=$hse kernel=$kernel" | tee -a "$LOG"; fi
  # once kernel reached, watch a bit more for USB/GUI then stop watcher (leave run_guest alive)
  if [ "$kernel" != 0 ] && [ $((i - kernel)) -ge 48 ]; then
    echo "=== watcher stop (kernel+4min) $(date '+%H:%M:%S'); run_guest pid=$RGPID STILL RUNNING for USB test ===" | tee -a "$LOG"; break
  fi
done
echo "=== fullboot watcher done $(date '+%H:%M:%S'); reads=$(grep -ca UsbBootReadBlocks "$LOG") hse=$(grep -ca 'usbsts=0x5' "$LOG") kernel=$kernel; run_guest LEFT RUNNING (do not kill until USB test done) ===" | tee -a "$LOG"
