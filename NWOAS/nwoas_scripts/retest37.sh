#!/bin/bash
# retest37 — H1: at ResetSystem(Shutdown), FbDumpDxe scans all guest UEFI RAM and counts how many
# of the 40494 golden boot.wim pages are present = RAMDisk boot.wim integrity at the failure moment.
#  RESETCOV found ~= 40494 => RAMDisk boot.wim INTACT end-to-end (closes 278 blind spot) => corruption downstream
#  RESETCOV found <  40216 => boot.wim pages corrupt/clobbered post-DMA => localize
# SAFE (SIGTERM only, NO SIGINT).
LOG=/private/tmp/claude-501/-Volumes-X31-NWOAS/08e1ec24-7e98-4f1e-91a5-4c4a8348b6ab/scratchpad/retest37-resetcov.log
: > "$LOG"
pkill -f run_guest 2>/dev/null; pkill -f "picocom.*debug-console" 2>/dev/null; sleep 1
echo "=== retest37: H1 reset-time golden RAM scan ===" >> "$LOG"
sudo /Volumes/X31/NWOAS/macvdmtool/macvdmtool reboot serial >> "$LOG" 2>&1
sleep 10
[ -e /dev/cu.debug-console ] || { echo "=== retest37 done (no serial) ===" >> "$LOG"; exit 1; }
pkill -f "picocom.*debug-console" 2>/dev/null || true
cd /Volumes/X31/NWOAS/m1n1_windows
M1N1DEVICE=/dev/cu.debug-console M1N1_KEEP_BAUD=1 .venv-hv/bin/python3 proxyclient/tools/run_guest.py -r m1n1-payload.bin -m pcie_emul.py >> "$LOG" 2>&1 &
RGPID=$!
CONCLUDED=""; PREV=0; STUCK=0
for i in $(seq 1 460); do
  sleep 5
  if grep -qaE 'HVLOG: RESETCOV' "$LOG"; then CONCLUDED="resetcov@iter$i"; sleep 15; break; fi
  if grep -qaE 'FBDUMP END' "$LOG"; then CONCLUDED="fbdump-no-resetcov@iter$i"; sleep 10; break; fi
  CUR=$(wc -l < "$LOG"); if [ "$CUR" -eq "$PREV" ]; then STUCK=$((STUCK+1)); else STUCK=0; PREV=$CUR; fi
  if [ "$STUCK" -ge 90 ]; then CONCLUDED="STUCK@iter$i(line$CUR)"; break; fi
  kill -0 $RGPID 2>/dev/null || { CONCLUDED="rg-exit@iter$i"; break; }
done
kill $RGPID 2>/dev/null; sleep 2; kill -9 $RGPID 2>/dev/null; pkill -f run_guest.py 2>/dev/null
echo "=== retest37 done ($CONCLUDED, iter $i) ===" >> "$LOG"
echo "---- H1 verdict ----" >> "$LOG"
echo "RESETCOV: $(grep -aoE 'RESETCOV found=[0-9]+/[0-9]+ scanned=[0-9]+' "$LOG" | tail -1)" >> "$LOG"
echo "(참고) DMA-time DMACOV 최종: $(grep -aoE 'DMACOV covered=[0-9]+/[0-9]+' "$LOG" | tail -1)" >> "$LOG"
echo "Shutdown: $(grep -acE 'ResetSystem2: ResetType Shutdown' "$LOG") | 최대LBA: $(grep -aoE 'LBA \(0x[0-9a-f]+\)' "$LOG" | grep -oE '0x[0-9a-f]+' | python3 -c 'import sys;v=[int(x,16) for x in sys.stdin];print("0x%x"%max(v) if v else "none")' 2>/dev/null)" >> "$LOG"