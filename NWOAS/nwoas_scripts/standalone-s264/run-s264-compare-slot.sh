#!/bin/bash
# restart running Windows (W4) -> wait launcher exit -> S264 dry-run compare -> reboot.py + S259 boot
#   bash standalone-s264/run-s264-compare-slot.sh <old .out> <old .link>
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts; R=$ROOT/standalone-s264/runs; mkdir -p $R
RES=$R/slot-$(date +%Y%m%d-%H%M%S).txt
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
python3 -I $ROOT/transport-s123/queue_job.py $2 $ROOT/layout-s231/jobs/W4-restart.cmd >/dev/null || { say "ABORT queue W4"; exit 3; }
for i in $(seq 1 200); do grep -q "LAUNCHER EXIT" $1 && break; sleep 2; done
grep -q "LAUNCHER EXIT" $1 || { say "ABORT old launcher did not exit"; exit 4; }
say "old launcher exited"
for i in $(seq 1 90); do [ -e /dev/cu.usbmodemC07HL05SQ6NY1 ] && [ -e /dev/cu.usbmodemC07HL05SQ6NY3 ] && break; sleep 2; done
sleep 6
caffeinate -i bash $ROOT/standalone-s264/s264-dryrun-compare.sh >> $RES 2>&1; say "compare rc=$?"
T=$R/boot-$(date +%H%M%S).txt
(nohup bash $ROOT/wifi-s259/run-s259.sh idle - > $T 2>&1 &)
for i in $(seq 1 240); do grep -a -q -E "S259 DONE|ABORT" $T && break; sleep 5; done
cat $T >> $RES
if ! grep -a -q "S259 DONE" $T; then say "retry idle boot"; T2=$R/boot2-$(date +%H%M%S).txt
  (nohup bash $ROOT/wifi-s259/run-s259.sh idle - > $T2 2>&1 &)
  for i in $(seq 1 240); do grep -a -q -E "S259 DONE|ABORT" $T2 && break; sleep 5; done; cat $T2 >> $RES; T=$T2; fi
say "SLOT DONE $(grep -a 'S259 DONE' $T | tail -1)"
