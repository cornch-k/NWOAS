#!/bin/bash
# NWOAS 4-1 / 4-2 rehearsal slots (PLAN-4-1-disk-boot.md 7장).  PREPARED, NOT RUN BY THE AGENT THAT WROTE IT.
#   bash run-stage1-rehearsal-slot.sh 42       <old .out> <old .link>
#        W4 restart -> run-stage1-42-rehearsal.sh (ends in Windows started by s264G alone, no MacBook link;
#        the user checks the screen, then restarts Windows from the Start menu to come back to D4)
#   bash run-stage1-rehearsal-slot.sh main     <old .out> <old .link>
#        W4 restart -> run-stage1-rehearsal.sh main -> (PASS only) run-s268-after-stage1.sh on the SAME boot
#        (S268 daily stack to "S268 DONE" with the NVMe QD engine, then Q1S / Q2 / chkdsk) -> Windows stays up
#   bash run-stage1-rehearsal-slot.sh negative <old .out> <old .link>
#        W4 restart -> run-stage1-rehearsal.sh missing -> fault -> run-s268.sh idle (reboot, normal daily boot)
#   ... <mode> idle -   the Mac mini already waits in a FRESH D4 proxy (e.g. after the 4-2 rehearsal the user
#        restarted Windows from the Start menu; that this returns to D4 under the C launcher is [가설]): no W4
# (copy of run-s264d-compare-slot.sh with the rehearsal in the middle)
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts; S=$ROOT/standalone-s264/stage1; R=$ROOT/standalone-s264/runs; mkdir -p $R
MODE=${1:?usage: run-stage1-rehearsal-slot.sh 42|main|negative <old .out>|idle <old .link>|-}
RES=$R/slot-stage1-$MODE-$(date +%Y%m%d-%H%M%S).txt
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
case "$MODE" in 42|main|negative) ;; *) echo "mode must be 42, main or negative"; exit 1;; esac
if [ "${2:-}" = idle ]; then
    [ -e /dev/cu.usbmodemC07HL05SQ6NY1 ] && [ -e /dev/cu.usbmodemC07HL05SQ6NY3 ] || { say "ABORT idle: no D4 proxy devices"; exit 3; }
    say "idle: using the waiting D4 proxy (no W4)"
else
    python3 -I $ROOT/transport-s123/queue_job.py $3 $ROOT/layout-s231/jobs/W4-restart.cmd >/dev/null || { say "ABORT queue W4"; exit 3; }
    for i in $(seq 1 200); do grep -q "LAUNCHER EXIT" $2 && break; sleep 2; done
    grep -q "LAUNCHER EXIT" $2 || { say "ABORT old launcher did not exit"; exit 4; }
    say "old launcher exited"
    for i in $(seq 1 90); do [ -e /dev/cu.usbmodemC07HL05SQ6NY1 ] && [ -e /dev/cu.usbmodemC07HL05SQ6NY3 ] && break; sleep 2; done
    sleep 6
fi
daily() { local T=$R/boot-$(date +%H%M%S).txt
  (nohup bash $ROOT/bt-s268/run-s268.sh idle - > $T 2>&1 &)
  for i in $(seq 1 240); do grep -a -q -E "S268 DONE|ABORT" $T && break; sleep 5; done
  cat $T >> $RES; say "daily boot: $(grep -a -E 'S268 DONE|ABORT' $T | tail -1)"; }
if [ "$MODE" = 42 ]; then
    caffeinate -i bash $S/run-stage1-42-rehearsal.sh >> $RES 2>&1; rc=$?; say "4-2 rehearsal rc=$rc"
    say "user: look at the Mac mini screen (Windows desktop = PASS), then restart Windows from the Start menu"
    exit $rc
fi
if [ "$MODE" = negative ]; then
    for v in missing fault; do
        caffeinate -i bash $S/run-stage1-rehearsal.sh $v >> $RES 2>&1; rc=$?; say "negative $v rc=$rc"
        [ $rc = 0 ] || { say "STOP: negative $v failed (rc $rc); power-cycle needed only for rc 2/5/6 without a proxy"; exit $rc; }
    done
    daily; exit 0
fi
caffeinate -i bash $S/run-stage1-rehearsal.sh main >> $RES 2>&1; rc=$?; say "main rehearsal rc=$rc"
if [ $rc != 0 ]; then
    [ $rc = 2 ] && { say "no proxy: power-cycle the Mac mini (nothing was written), then the daily boot"; exit 2; }
    say "rehearsal failed (rc $rc): back to the normal daily boot"; daily; exit $rc
fi
T=$R/st1-s268-$(date +%H%M%S).txt
(nohup bash $S/run-s268-after-stage1.sh > $T 2>&1 &)
for i in $(seq 1 600); do grep -a -q -E "ST1-S268 DONE|ST1-S268 ABORT" $T && break; sleep 5; done
cat $T >> $RES
say "SLOT DONE $(grep -a -E 'ST1-S268 DONE|ST1-S268 ABORT' $T | tail -1)"
