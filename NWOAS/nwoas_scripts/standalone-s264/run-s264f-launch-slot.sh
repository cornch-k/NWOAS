#!/bin/bash
# NEEDS THE USER WATCHING THE MAC MINI SCREEN (real C launch of Windows).
# S264E (copy of run-s264d-launch-slot.sh; was: copy of run-s264c-launch-slot.sh): restart running Windows (W4) -> wait launcher exit -> s264f-launch-watch.sh (C launcher starts Windows; MacBook only watches)
#   bash standalone-s264/run-s264f-launch-slot.sh <old .out> <old .link>     (S264C_FATAL=1 / S264D_NO_WIFI=1 / S264D_NO_AUDIO_B=1 / S264D_AUDIO_PROT=... / S264E_NO_BT=1 / S264E_S259=1 / S264E_NO_FENCE_WR=1 / S264F_NO_PWR=1)
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts; R=$ROOT/standalone-s264/runs; mkdir -p $R
RES=$R/launch-slot-s264f-$(date +%Y%m%d-%H%M%S).txt
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
python3 -I $ROOT/transport-s123/queue_job.py $2 $ROOT/layout-s231/jobs/W4-restart.cmd >/dev/null || { say "ABORT queue W4"; exit 3; }
for i in $(seq 1 200); do grep -q "LAUNCHER EXIT" $1 && break; sleep 2; done
grep -q "LAUNCHER EXIT" $1 || { say "ABORT old launcher did not exit"; exit 4; }
say "old launcher exited"
for i in $(seq 1 90); do [ -e /dev/cu.usbmodemC07HL05SQ6NY1 ] && [ -e /dev/cu.usbmodemC07HL05SQ6NY3 ] && break; sleep 2; done
sleep 6
say "launch-watch start"
caffeinate -dims bash $ROOT/standalone-s264/s264f-launch-watch.sh >> $RES 2>&1; say "launch-watch exit rc=$?"
