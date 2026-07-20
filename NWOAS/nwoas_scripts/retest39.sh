#!/bin/bash
# retest39 — H2: at shutdown, scan guest RAM for decompressed winload.efi pages (SET B/WLCOV).
LOG=/private/tmp/claude-501/-Volumes-X31-NWOAS/08e1ec24-7e98-4f1e-91a5-4c4a8348b6ab/scratchpad/retest39-h2.log
: > "$LOG"
pkill -f run_guest 2>/dev/null; pkill -f "picocom.*debug-console" 2>/dev/null; sleep 1
echo "=== retest39: H2 winload-buffer scan (WLCOV) ===" >> "$LOG"
sudo /Volumes/X31/NWOAS/macvdmtool/macvdmtool reboot serial >> "$LOG" 2>&1
sleep 10
[ -e /dev/cu.debug-console ] || { echo "=== retest39 done (no serial) ===" >> "$LOG"; exit 1; }
pkill -f "picocom.*debug-console" 2>/dev/null || true
cd /Volumes/X31/NWOAS/m1n1_windows
M1N1DEVICE=/dev/cu.debug-console M1N1_KEEP_BAUD=1 .venv-hv/bin/python3 proxyclient/tools/run_guest.py -r m1n1-payload.bin -m pcie_emul.py >> "$LOG" 2>&1 &
RGPID=$!
CONCLUDED=""; PREV=0; STUCK=0
for i in $(seq 1 380); do
  sleep 5
  if grep -qaE 'HVLOG: WLCOV found' "$LOG"; then CONCLUDED="wlcov@iter$i"; sleep 10; break; fi
  if grep -qaE 'FBDUMP END' "$LOG"; then CONCLUDED="fbdump@iter$i"; sleep 12; break; fi
  CUR=$(wc -l < "$LOG"); if [ "$CUR" -eq "$PREV" ]; then STUCK=$((STUCK+1)); else STUCK=0; PREV=$CUR; fi
  if [ "$STUCK" -ge 95 ]; then CONCLUDED="STUCK@iter$i(line$CUR)"; break; fi
  kill -0 $RGPID 2>/dev/null || { CONCLUDED="rg-exit@iter$i"; break; }
done
kill $RGPID 2>/dev/null; sleep 2; kill -9 $RGPID 2>/dev/null; pkill -f run_guest.py 2>/dev/null
echo "=== retest39 done ($CONCLUDED, iter $i) ===" >> "$LOG"
echo "H2 WLCOV: $(grep -aoE 'WLCOV found=[0-9]+/[0-9]+ firstAddr=0x[0-9a-f]+' "$LOG" | head -1)" >> "$LOG"
echo "DMACOV: $(grep -aoE 'DMACOV covered=[0-9]+/[0-9]+' "$LOG" | tail -1) | Shutdown: $(grep -acE 'ResetSystem2: ResetType Shutdown' "$LOG") | FBDUMP: $(grep -acE 'FBDUMP START' "$LOG")" >> "$LOG"
