#!/bin/bash
# NWOAS deferred-approach test (2026-07-10 pivot away from Method A).
# Payload m1n1-payload-deferred-v1.bin = UEFI FULL RAM (high RAM SYSTEM_MEMORY, NWOAS_METHOD_A=0)
#   + [0,4GB) window kept as the SOLE sub-4GB pool for Windows + window-aware DART + EBS static window.
# HYPOTHESIS: high RAM Conventional (not Reserved) -> UEFI runs in FL1100-reachable RAM -> UEFI USB
#   reads work like no-EXP-5 (past the 2304 HSE stall), while Windows still gets the low window.
# SIGNALS (per-attempt deltas):
#   SUCCESS_KERNEL : winload / "Windows is loading" / elr=0xfffff8  (past the wedge into the kernel)
#   SUCCESS_READS  : UsbBootReadBlocks >> 2304 (>8000) with usbsts=0x5 (HSE) near 0  (Piece A works)
#   HSE_RECURS     : usbsts=0x5 storm + UsbBootReadBlocks frozen <4000 (same as v32/Method A failure)
#   FLAKY          : _start crash / run_guest early exit / no bootmgfw -> retry (<=3 attempts)
# Rules: 115200 (M1N1_KEEP_BAUD=1, transport-capped); no reboot-hammer (<=3 attempts then stop).
set -u
PAYLOAD=m1n1-payload-deferred-v1.bin
VDM=/Volumes/X31/NWOAS/macvdmtool/macvdmtool
LOG=/Volumes/X31/NWOAS/nwoas_scripts/logs/deferred-v1.log
mkdir -p /Volumes/X31/NWOAS/nwoas_scripts/logs
: > "$LOG"
echo "=== deferred-v1 test start $(date '+%H:%M:%S') payload=$PAYLOAD ===" | tee -a "$LOG"

VERDICT=UNKNOWN
for attempt in 1 2 3; do
  echo "=== attempt $attempt reboot+run $(date '+%H:%M:%S') ===" | tee -a "$LOG"
  pkill -f run_guest 2>/dev/null; sleep 1
  sudo -n "$VDM" reboot serial >> "$LOG" 2>&1; sleep 10
  [ -e /dev/cu.debug-console ] || { echo "no serial; abort" | tee -a "$LOG"; VERDICT=NO_SERIAL; break; }
  base_reads=$(grep -ca "UsbBootReadBlocks" "$LOG"); base_hse=$(grep -ca "usbsts=0x5" "$LOG")
  cd /Volumes/X31/NWOAS/m1n1_windows
  M1N1DEVICE=/dev/cu.debug-console NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000 M1N1_KEEP_BAUD=1 NWOAS_LOG="$LOG" \
    .venv-hv/bin/python3 -u proxyclient/tools/run_guest.py \
    -r "$PAYLOAD" -m pcie_emul-exp5.py >> "$LOG" 2>&1 &
  RGPID=$!
  echo "run_guest pid=$RGPID (attempt $attempt)" | tee -a "$LOG"
  loaded=0; prev_reads=0; frozen=0
  for i in $(seq 1 240); do   # 240*5s = 20min cap per attempt
    sleep 5
    kill -0 $RGPID 2>/dev/null || { echo "run_guest exited @iter$i $(date '+%H:%M:%S')" | tee -a "$LOG"; break; }
    reads=$(( $(grep -ca "UsbBootReadBlocks" "$LOG") - base_reads ))
    hse=$(( $(grep -ca "usbsts=0x5" "$LOG") - base_hse ))
    if [ "$loaded" = 0 ] && grep -qa "EntryPoint=0x0001002\|bootmgfw" "$LOG"; then
      loaded=$i; echo "=== bootmgfw loaded @iter$i reads=$reads $(date '+%H:%M:%S') ===" | tee -a "$LOG"
    fi
    # SUCCESS-hard: past the wedge into winload/kernel
    if grep -qa "winload\|Windows is loading\|elr=0xfffff8" "$LOG"; then
      echo "=== SUCCESS_KERNEL: PAST WEDGE @iter$i reads=$reads hse=$hse $(date '+%H:%M:%S') ===" | tee -a "$LOG"
      sleep 10; VERDICT=SUCCESS_KERNEL; break
    fi
    # SUCCESS-soft: reads well past the 2304 stall, HSE near 0
    if [ "$reads" -gt 8000 ] && [ "$hse" -lt 30 ]; then
      echo "=== SUCCESS_READS: reads=$reads (>>2304) hse=$hse @iter$i $(date '+%H:%M:%S') ===" | tee -a "$LOG"
      sleep 10; VERDICT=SUCCESS_READS; break
    fi
    # HSE_RECURS: HSE storm + reads frozen low
    if [ "$hse" -gt 80 ] && [ "$reads" = "$prev_reads" ]; then frozen=$((frozen+1)); else frozen=0; fi
    if [ "$frozen" -ge 18 ] && [ "$reads" -lt 4000 ]; then
      echo "=== HSE_RECURS: hse=$hse reads=$reads frozen90s @iter$i $(date '+%H:%M:%S') ===" | tee -a "$LOG"
      VERDICT=HSE_RECURS; break
    fi
    prev_reads=$reads
    [ $((i % 6)) = 0 ] && echo "  [iter$i $(date '+%H:%M:%S')] reads=$reads hse=$hse loaded=$loaded" | tee -a "$LOG"
  done
  pkill -f run_guest 2>/dev/null
  case "$VERDICT" in
    SUCCESS_KERNEL|SUCCESS_READS|HSE_RECURS|NO_SERIAL) break ;;
    *) # timed out or early exit; if reads made real progress past stall, note PARTIAL and stop, else retry
       rr=$(( $(grep -ca "UsbBootReadBlocks" "$LOG") - base_reads )); hh=$(( $(grep -ca "usbsts=0x5" "$LOG") - base_hse ))
       if [ "$rr" -gt 2304 ] && [ "$hh" -lt 30 ]; then
         echo "=== PARTIAL_PROGRESS attempt $attempt reads=$rr hse=$hh (past stall, slow) ===" | tee -a "$LOG"; VERDICT=PARTIAL_PROGRESS; break
       fi
       echo "=== attempt $attempt inconclusive (flaky _start/wedge? reads=$rr hse=$hh); retry ===" | tee -a "$LOG" ;;
  esac
done
echo "=== deferred-v1 VERDICT=$VERDICT $(date '+%H:%M:%S') ===" | tee -a "$LOG"
echo "--- totals: UsbBootReadBlocks=$(grep -ca UsbBootReadBlocks "$LOG") usbsts=0x5(HSE)=$(grep -ca 'usbsts=0x5' "$LOG") bootmgfw=$(grep -ca bootmgfw "$LOG") winload=$(grep -ca winload "$LOG") ---" | tee -a "$LOG"
