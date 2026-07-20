#!/bin/bash
# NWOAS kmutil-FREE tethered-hv runner.
#
# Loads a FRESH device-side m1n1 (EL2 hypervisor, incl. vGIC + any src/ C changes
# such as hv_vm.c / hv_exc.c / hv_vgic.c) over serial via the hardened
# chainload.py, then runs the guest. This replaces the kmutil + 1TR round for
# iterating on hv C code. Requires ONE stable bootstrap m1n1 already
# kmutil-installed as the boot object (any recent build works as the proxy
# bootstrap — e.g. the §6-A build installed this round).
#
# Flow:  macvdmtool reboot serial  (boot installed bootstrap m1n1 -> "Running proxy...")
#     -> chainload.py -r build/m1n1.bin  (gzip'd ~222KB upload + jump to FRESH hv, ~20-40s @115200)
#     -> run_guest.py -r PAYLOAD -m MODULE  (guest under the freshly-chainloaded hv)
#
# vGIC is preserved: it is device-side code in whatever m1n1 is running, and
# hv.init()/hv.start() (proxy commands) bring it up identically to a kmutil boot
# (ENABLE_VGIC_MODULE is unconditionally defined; investigated 2026-07-12).
#
# ⚠ HONESTY: this is UNPROVEN on this hardware yet. chainload over the flaky VDM
# bridge may need a re-run if a byte drops the ~222KB upload (ST_XFRERR). If it
# fails, fall back to kmutil for that round. Build success / clean chainload !=
# boot success — only the real M1 (cursor / EVTDUMP lines) judges.
#
# Override target with:  NWOAS_PAYLOAD=... NWOAS_MODULE=... ./run-hv-chainload.sh
set -u
ROOT=/Volumes/X31/NWOAS
M1N1="$ROOT/m1n1_windows"
VDM="$ROOT/macvdmtool/macvdmtool"
DEV=/dev/cu.debug-console
PY="$M1N1/.venv-hv/bin/python3"
HV_FRESH="$M1N1/build/m1n1.bin"            # fresh EL2 hv to chainload (carries the new src/ changes)
PAYLOAD="${NWOAS_PAYLOAD:-$M1N1/m1n1-payload-iort-noleafdma-v1.bin}"
MODULE="${NWOAS_MODULE:-$M1N1/pcie_emul-exp5-evtdump.py}"
STAMP=$(date '+%H%M')
LOG="$ROOT/nwoas_scripts/logs/chainload-hv-$STAMP.log"
: > "$LOG"
echo "=== kmutil-FREE hv run $(date '+%H:%M:%S') fresh-hv=$HV_FRESH ===" | tee -a "$LOG"
echo "    payload=$PAYLOAD" | tee -a "$LOG"
echo "    module=$MODULE" | tee -a "$LOG"

pkill -f run_guest 2>/dev/null; pkill -f "picocom.*debug-console" 2>/dev/null; sleep 1
PW="$(cat "$ROOT/.env")"
printf '%s\n' "$PW" | sudo -S -p '' "$VDM" reboot serial >> "$LOG" 2>&1
PW=""
sleep 8
[ -e "$DEV" ] || { echo "NO SERIAL $DEV (cable loose?)" | tee -a "$LOG"; exit 1; }
pkill -f "picocom.*debug-console" 2>/dev/null || true

# [1] chainload the FRESH hv. KEEP_BAUD=1 is REQUIRED (else bootstrap_port in
# m1n1.setup promotes to 1.5M, which the VDM bridge drops). chainload.py now
# warns-not-crashes on a flaky confirm nop, so `|| true` is only a backstop.
echo "=== [1/2] chainload fresh m1n1 hv over serial (~20-40s) $(date '+%H:%M:%S') ===" | tee -a "$LOG"
M1N1DEVICE="$DEV" env M1N1_KEEP_BAUD=1 "$PY" -u "$M1N1/proxyclient/tools/chainload.py" \
  -r "$HV_FRESH" >> "$LOG" 2>&1 \
  || echo "  (chainload exit nonzero — continuing; run_guest re-bootstraps the link)" | tee -a "$LOG"
if grep -qa "Proxy is alive again\|WARNING: chainload confirm" "$LOG"; then
  echo "  chainload jumped into fresh hv (proxy re-announced)" | tee -a "$LOG"
else
  echo "  ⚠ no proxy re-announce seen — upload may have failed (VDM drop); re-run or fall back to kmutil" | tee -a "$LOG"
fi
sleep 2

# [2] run the guest under the freshly-chainloaded hv. run_guest's bootstrap_port
# (15-retry resync) re-establishes framing regardless of chainload's confirm.
echo "=== [2/2] run_guest under fresh hv $(date '+%H:%M:%S') ===" | tee -a "$LOG"
M1N1DEVICE="$DEV" env NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000 M1N1_KEEP_BAUD=1 NWOAS_LOG="$LOG" \
  nohup "$PY" -u "$M1N1/proxyclient/tools/run_guest.py" -r "$PAYLOAD" -m "$MODULE" >> "$LOG" 2>&1 &
RGPID=$!
echo "run_guest pid=$RGPID" | tee -a "$LOG"

for i in $(seq 1 540); do
  sleep 5
  kill -0 $RGPID 2>/dev/null || { echo "run_guest exited @iter$i $(date '+%H:%M:%S')" | tee -a "$LOG"; break; }
  DUMPS=$(grep -ca 'HVLOG: EVTDUMP erstba' "$LOG")
  if [ "$DUMPS" -ge 16 ]; then
    echo "=== ★16 EVTDUMPS DONE @iter$i $(date '+%H:%M:%S') ===" | tee -a "$LOG"; sleep 6; break
  fi
  if [ $((i % 12)) = 0 ]; then
    echo "  [iter$i $(date '+%H:%M:%S')] kernel=$(grep -ca 'elr=0xfffff8' $LOG) FLC=$(grep -ca 'NWOAS-FLC' $LOG) EVTDUMP=$DUMPS" | tee -a "$LOG"
  fi
done
echo "=== chainload-hv watcher done $(date '+%H:%M:%S') ===" | tee -a "$LOG"
echo "--- EVTDUMP summary ---" | tee -a "$LOG"
grep -a 'HVLOG: EVTDUMP' "$LOG" | tail -60 | tee -a "$LOG"
