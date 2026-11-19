#!/bin/bash
# NWOAS 4-1 rehearsal (PLAN-4-1-disk-boot.md 7장) - PREPARED, NOT RUN BY THE AGENT THAT WROTE IT.
#   bash run-stage1-rehearsal.sh main | missing | fault
# The MacBook chainloads a first-stage m1n1 from the running D4 proxy, exactly as it chainloads the HV today.
#   main     stage1.bin (the file go1.sh registers): must read ESP m1n1/boot.bin (= D4) and hand over to it;
#            PASS (rc 0) = afterwards the D4 identity probe and its layout gate (G-2) pass.  The Mac mini is
#            left in that D4 proxy WITHOUT a reboot: run run-s268-after-stage1.sh next (S268 + SSD checks).
#   missing  stage1-missing.bin (chainload=<ESP>;m1n1/nwoas-absent.bin): the file is not there
#   fault    stage1-fault.bin (nwoas_stage1_fault=1): a data abort on purpose just before the ESP read
#            negative PASS (rc 0) = stage 1 did NOT reboot: its own USB fallback proxy answers (marker
#            "NWOAS-STAGE1: USB proxy fallback" at its base), then the MacBook chainloads D4 (esp-4-1/boot.bin)
#            and the D4 probe passes = the recovery the user would get after a bad ESP in the real boot.
# Writes nothing to any disk, changes no boot policy.  Needs: the Mac mini in the D4 proxy (W4 first), the
# esp-4-1 jobs done (NWOAS ESP-4-1 COPY COMPLETE).
# rc: 0 PASS | 2 no proxy came back (power-cycle; nothing written) | 3 main: proxy back but not D4 |
#     4 main: D4 back but layout gate differs | 5 negative: proxy back but not stage 1 (= it rebooted or
#     ignored the fault) | 6 negative: stage 1 fell back but the D4 restore failed | 1 STOP before anything
set -eu
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS
M1N1="$ROOT/m1n1_windows"
HERE="$ROOT/nwoas_scripts/standalone-s264/stage1"
DEV=/dev/cu.usbmodemC07HL05SQ6NY1
VUART=/dev/cu.usbmodemC07HL05SQ6NY3
PY="$M1N1/.venv-hv/bin/python3"
S244="$ROOT/nwoas_scripts/display-s244"
D4="$HERE/esp-4-1/boot.bin"
V="${1:?usage: run-stage1-rehearsal.sh main|missing|fault}"
case "$V" in
    main)    ST1="$HERE/esp-4-1/stage1.bin";;
    missing) ST1="$HERE/rehearsal/stage1-missing.bin";;
    fault)   ST1="$HERE/rehearsal/stage1-fault.bin";;
    *) echo "usage: run-stage1-rehearsal.sh main|missing|fault"; exit 1;;
esac
OUT="$ROOT/nwoas_scripts/logs/stage1-rehearsal-$V-$(date '+%Y%m%d-%H%M%S')"
export M1N1DEVICE="$DEV" M1N1_KEEP_BAUD=1 M1N1_SPLIT_CONSOLE=1 PYTHONDONTWRITEBYTECODE=1
export PYTHONPATH="$M1N1/proxyclient:$ROOT/nwoas_scripts"
[ -e "$DEV" ] || { echo 'STOP: direct CDC proxy missing'; exit 1; }
( cd "$HERE" && shasum -a 256 -c --quiet SHA256SUMS ) || { echo 'STOP: a stage-1 file is not the recorded one'; exit 1; }
[ "$(md5 -q "$D4")" = 7bec0225edc101857be3b7f195dd7357 ] || { echo 'STOP: esp-4-1/boot.bin is not D4'; exit 1; }
[ "$(md5 -q "$ROOT/experiments/tahoe-afk-20260906/m1n1-dcp-defer.bin")" = 7bec0225edc101857be3b7f195dd7357 ]
if /usr/sbin/lsof -t "$DEV" "$VUART" >/dev/null 2>&1; then echo 'STOP: CDC pipe busy'; exit 1; fi
echo "Log: $OUT.log"
"$PY" -u "$ROOT/nwoas_scripts/recovery-s126/proxy_ready.py" >> "$OUT.log" 2>&1
RC=0
"$PY" -B -u "$S244/probe_fw135_s244.py" > "$OUT.probe-before" 2>&1 || RC=$?
cat "$OUT.probe-before" >> "$OUT.log"
[ "$RC" -eq 0 ] || { echo "STOP: the running m1n1 is not the D4 proxy (probe exit $RC); nothing chainloaded"; exit 1; }
echo "[4-1 rehearsal $V] D4 proxy confirmed; chainloading $(basename "$ST1")" | tee -a "$OUT.log"
"$PY" -u "$M1N1/proxyclient/tools/chainload.py" -r "$ST1" >> "$OUT.log" 2>&1 || true
grep -qa 'Proxy is alive again\|WARNING: chainload confirm' "$OUT.log" \
    || { echo "[4-1 rehearsal $V] FAIL: no proxy came back after the chainload (power-cycle the Mac mini; nothing was written)" | tee -a "$OUT.log"; exit 2; }
sleep 3
"$PY" -u "$ROOT/nwoas_scripts/recovery-s126/proxy_ready.py" >> "$OUT.log" 2>&1 || true

if [ "$V" != main ]; then
    RC=0
    "$PY" -B -u "$HERE/probe_running_image.py" "$ST1" "NWOAS-STAGE1: USB proxy fallback" >> "$OUT.log" 2>&1 || RC=$?
    if [ "$RC" -ne 0 ]; then
        echo "[4-1 rehearsal $V] FAIL: the proxy that came back is not stage 1's fallback proxy (probe exit $RC)." | tee -a "$OUT.log"
        echo "  Stage 1 most likely rebooted (11-16 reboot-loop kind) or did not take the failure path. Do NOT go to 1TR." | tee -a "$OUT.log"
        exit 5
    fi
    echo "[4-1 rehearsal $V] stage 1 fell back to its USB proxy (no reboot); chainloading D4 back" | tee -a "$OUT.log"
    "$PY" -u "$M1N1/proxyclient/tools/chainload.py" -r "$D4" >> "$OUT.log" 2>&1 || true
    sleep 3
    "$PY" -u "$ROOT/nwoas_scripts/recovery-s126/proxy_ready.py" >> "$OUT.log" 2>&1 || true
    RC=0
    "$PY" -B -u "$S244/probe_fw135_s244.py" > "$OUT.probe-after" 2>&1 || RC=$?
    cat "$OUT.probe-after" >> "$OUT.log"
    [ "$RC" -eq 0 ] || { echo "[4-1 rehearsal $V] FAIL: D4 restore from the stage-1 proxy failed (probe exit $RC); power-cycle" | tee -a "$OUT.log"; exit 6; }
    echo "[4-1 rehearsal $V] NEGATIVE PASS: failure -> stage-1 USB proxy -> MacBook chainloaded D4 -> D4 probe pass" | tee -a "$OUT.log"
    exit 0
fi

RC=0
"$PY" -B -u "$S244/probe_fw135_s244.py" > "$OUT.probe-after" 2>&1 || RC=$?
cat "$OUT.probe-after" >> "$OUT.log"
if [ "$RC" -eq 0 ]; then
    echo "[4-1 rehearsal main] PASS: after stage1 the D4 proxy runs again = stage1 read ESP m1n1/boot.bin and handed over" | tee -a "$OUT.log"
    echo "[4-1 rehearsal main] (rehearsal only: not proof of the 1TR registration or of a boot). Next, no reboot: run-s268-after-stage1.sh" | tee -a "$OUT.log"
    exit 0
fi
if [ "$RC" -eq 4 ]; then
    echo "[4-1 rehearsal main] HAND-OVER OK BUT LAYOUT GATE DIFFERS: D4 runs again, but the probe's memory-layout gate (G-2)" | tee -a "$OUT.log"
    echo "  fails after the extra chainload hop; today's launchers would refuse. Do NOT go to 1TR; compare" | tee -a "$OUT.log"
    echo "  $OUT.probe-before and $OUT.probe-after first." | tee -a "$OUT.log"
    exit 4
fi
echo "[4-1 rehearsal main] FAIL: the proxy that came back is not D4 (probe exit $RC) - stage1 most likely did not" | tee -a "$OUT.log"
echo "  find/read ESP m1n1/boot.bin and fell back to its own proxy. Do NOT go to 1TR. Log: $OUT.log" | tee -a "$OUT.log"
exit 3
