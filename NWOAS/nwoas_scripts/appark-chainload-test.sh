#!/bin/bash
# NWOAS Layer-1 (AP-park) validation via kmutil-FREE chainload.
#
# Tests the build/m1n1.bin (Layer-1 = coherence-fix + runaway-AP auto-park) by chainloading
# it over serial onto whatever bootstrap m1n1 is currently installed, then running the guest.
#
# Judgement signals (watched below):
#   [PARK]  "NWOAS parking runaway guest cpu"     = Layer-1 fired (a storming AP was parked)
#   [WEDGE] heartbeats (usb-bridge/fl1100-bridge/EVTDUMP) STOP for a long stretch = still wedged
#   [KERN]  kernel elr=0xfffff8 count climbing     = guest kernel progressing
#   [LIVE]  guest reaches Windows installer (FLC markers keep changing, EVTDUMP populates)
#
# The whole point: with Layer-1, a storming AP should be PARKED and the boot CPU should keep
# progressing (kernel count keeps climbing, heartbeats keep beating) instead of global wedge.
#
# no-hammer: one reboot serial (this script's own). If chainload upload drops (VDM), it retries
# 3x WITHOUT another reboot. If that fails, it does NOT reboot again — falls back (manual).
set -u
ROOT=/Volumes/X31/NWOAS
M1N1="$ROOT/m1n1_windows"
VDM="$ROOT/macvdmtool/macvdmtool"
DEV=/dev/cu.debug-console
PY="$M1N1/.venv-hv/bin/python3"
HV_FRESH="$M1N1/build/m1n1.bin"
PAYLOAD="${NWOAS_PAYLOAD:-$M1N1/m1n1-payload-iort-noleafdma-v1.bin}"
MODULE="${NWOAS_MODULE:-$M1N1/pcie_emul-exp5-evtdump.py}"
STAMP=$(date '+%H%M')
LOG="$ROOT/nwoas_scripts/logs/appark-chainload-$STAMP.log"
: > "$LOG"
echo "=== Layer-1 AP-park chainload test $(date '+%H:%M:%S') fresh-hv=$HV_FRESH ===" | tee -a "$LOG"
echo "    payload=$PAYLOAD module=$MODULE" | tee -a "$LOG"
echo "    build/m1n1.bin sha=$(shasum "$HV_FRESH" | cut -c1-12)" | tee -a "$LOG"

pkill -f run_guest 2>/dev/null; pkill -f "picocom.*debug-console" 2>/dev/null; sleep 1
PW="$(cat "$ROOT/.env")"
printf '%s\n' "$PW" | sudo -S -p '' "$VDM" reboot serial >> "$LOG" 2>&1
PW=""
sleep 9
[ -e "$DEV" ] || { echo "NO SERIAL $DEV (cable loose?)" | tee -a "$LOG"; exit 1; }
pkill -f "picocom.*debug-console" 2>/dev/null || true

# chainload the fresh Layer-1 hv (retry up to 3x, no reboot between)
CL_OK=0
for try in 1 2 3; do
  echo "=== [chainload try $try] $(date '+%H:%M:%S') ===" | tee -a "$LOG"
  M1N1DEVICE="$DEV" env M1N1_KEEP_BAUD=1 PYTHONPATH="$M1N1/proxyclient" "$PY" -u \
    "$M1N1/proxyclient/tools/chainload.py" -r "$HV_FRESH" >> "$LOG" 2>&1
  if grep -qa "Proxy is alive again\|WARNING: chainload confirm" "$LOG"; then
    echo "  chainload OK (fresh hv jumped) try $try" | tee -a "$LOG"; CL_OK=1; break
  fi
  echo "  chainload try $try failed (VDM drop?), retry (no reboot)..." | tee -a "$LOG"; sleep 2
done
[ "$CL_OK" = 1 ] || { echo "=== chainload failed 3x — NOT rebooting (no-hammer). Manual fallback. ===" | tee -a "$LOG"; exit 2; }
sleep 2

echo "=== [run_guest] under freshly-chainloaded Layer-1 hv $(date '+%H:%M:%S') ===" | tee -a "$LOG"
M1N1DEVICE="$DEV" env NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000 M1N1_KEEP_BAUD=1 NWOAS_LOG="$LOG" \
  PYTHONPATH="$M1N1/proxyclient" nohup "$PY" -u "$M1N1/proxyclient/tools/run_guest.py" \
  -r "$PAYLOAD" -m "$MODULE" >> "$LOG" 2>&1 &
RGPID=$!
echo "run_guest pid=$RGPID" | tee -a "$LOG"

PREV_KERN=0; PREV_FLC=0; STALL=0
for i in $(seq 1 600); do
  sleep 5
  kill -0 $RGPID 2>/dev/null || { echo "run_guest exited @iter$i $(date '+%H:%M:%S')" | tee -a "$LOG"; break; }
  KERN=$(grep -ca 'elr=0xfffff8' "$LOG")
  FLC=$(grep -ca 'NWOAS-FLC' "$LOG")
  PARK=$(grep -ca 'NWOAS parking runaway' "$LOG")
  DUMPS=$(grep -ca 'HVLOG: EVTDUMP erstba' "$LOG")
  UNMAP=$(grep -ca 'Unmapped IPA' "$LOG")
  # §6-D: NCREMAP = how many times the stage-2 non-cacheable remap actually fired (0 = code path
  # never ran, e.g. gate off or latch never happened). SLOT = DCBAADIAG[1] seen non-zero (device
  # slot allocated = Enable Slot succeeded = the actual breakthrough signal, stronger than EVTDUMP).
  NCREMAP=$(grep -ca 'HVLOG: NCREMAP' "$LOG")
  SLOT=$(grep -a 'HVLOG: DCBAADIAG \[1\]=0x' "$LOG" | grep -vc '\[1\]=0x0$')
  # progress detector: kernel or FLC advancing = alive; both frozen for long = possible wedge
  if [ "$KERN" = "$PREV_KERN" ] && [ "$FLC" = "$PREV_FLC" ]; then STALL=$((STALL+1)); else STALL=0; fi
  PREV_KERN=$KERN; PREV_FLC=$FLC
  if [ "$PARK" -gt 0 ] && [ $((i % 6)) = 0 ]; then
    echo "  ★[iter$i] PARK=$PARK fired (Layer-1 active) kernel=$KERN FLC=$FLC EVTDUMP=$DUMPS unmap=$UNMAP" | tee -a "$LOG"
  fi
  if [ $((i % 12)) = 0 ]; then
    echo "  [iter$i $(date '+%H:%M:%S')] kernel=$KERN FLC=$FLC PARK=$PARK EVTDUMP=$DUMPS unmap=$UNMAP ncremap=$NCREMAP slot=$SLOT stall=$STALL" | tee -a "$LOG"
  fi
  if [ "$SLOT" -gt 0 ]; then
    echo "=== ★★★ SLOT ALLOCATED @iter$i $(date '+%H:%M:%S') — DCBAADIAG[1]!=0, Enable Slot succeeded, §3.2 breakthrough candidate ===" | tee -a "$LOG"; sleep 10; break
  fi
  if [ "$DUMPS" -ge 16 ]; then
    echo "=== ★16 EVTDUMPS @iter$i $(date '+%H:%M:%S') — decisive coherence data captured ===" | tee -a "$LOG"; sleep 6; break
  fi
  # stall for ~5 min (60 iters) after kernel reached = report likely wedge and keep watching a bit
  if [ "$STALL" -ge 60 ] && [ "$KERN" -gt 50 ]; then
    echo "  ⚠[iter$i] STALL $((STALL*5))s with no kernel/FLC progress (kernel=$KERN) — possible wedge; watching..." | tee -a "$LOG"
    STALL=30
  fi
done
echo "=== appark-chainload watcher done $(date '+%H:%M:%S') ===" | tee -a "$LOG"
echo "--- summary: kernel=$(grep -ca 'elr=0xfffff8' "$LOG") PARK=$(grep -ca 'NWOAS parking runaway' "$LOG") EVTDUMP=$(grep -ca 'HVLOG: EVTDUMP erstba' "$LOG") unmap=$(grep -ca 'Unmapped IPA' "$LOG") ncremap=$(grep -ca 'HVLOG: NCREMAP' "$LOG") slot=$(grep -a 'HVLOG: DCBAADIAG \[1\]=0x' "$LOG" | grep -vc '\[1\]=0x0$') ---" | tee -a "$LOG"
grep -a 'NWOAS parking runaway\|HVLOG: EVTDUMP\|HVLOG: NCREMAP\|HVLOG: DCBAADIAG' "$LOG" | tail -60 | tee -a "$LOG"
