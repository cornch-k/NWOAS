#!/bin/bash
# S248: restart Windows -> agxmed-s247 (no delivery) -> D28 (arm + restart GPU, which works from the failed state) -> stats.
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
OLDOUT=$1; OLDLINK=$2; TAG=$3
RES=$ROOT/agx-s248/runs/reboot-$TAG.txt
W4=$ROOT/layout-s231/jobs/W4-restart.cmd
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
jobwait() { local L=$1 j; j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
  for i in $(seq 1 480); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && break; sleep 5; done
  JOBLOG=$L/job-$j.log; }
say "START old=$OLDLINK"
python3 -I $ROOT/transport-s123/queue_job.py $OLDLINK $W4 >/dev/null || { say "ABORT queue W4"; exit 3; }
for i in $(seq 1 200); do grep -q "LAUNCHER EXIT" $OLDOUT && break; sleep 2; done
grep -q "LAUNCHER EXIT" $OLDOUT || { say "ABORT: old launcher did not exit"; exit 4; }
for i in $(seq 1 90); do [ -e /dev/cu.usbmodemC07HL05SQ6NY1 ] && [ -e /dev/cu.usbmodemC07HL05SQ6NY3 ] && break; sleep 2; done
sleep 4
CUR=$ROOT/logs/launcher-agxmed-$TAG-$(date "+%Y%m%d-%H%M%S").out
(caffeinate -dims bash $ROOT/agxmed-s247-guest-test.sh > $CUR 2>&1; echo "LAUNCHER EXIT $?" >> $CUR) &
say "launched $CUR"
L=""
for i in $(seq 1 240); do LOG=$(sed -n "s/^Log: //p" $CUR | head -1); [ -n "$LOG" ] && L=$LOG.link
  [ -n "$L" ] && grep -q '"kind": 4' $L/events.jsonl 2>/dev/null && break
  grep -q "LAUNCHER EXIT" $CUR && { say "ABORT: launcher exited before worker"; exit 2; }
  sleep 5; done
say "worker connected link=$L"
python3 -I $ROOT/transport-s123/queue_job.py $L $ROOT/agx-s248/jobs/D28-rearm.cmd >/dev/null; jobwait $L
iconv -f CP949 -t UTF-8 $JOBLOG | tr -d '\r' | grep -a -E "DEV|Wom1|DWM" | tee -a $RES
say "ARMED DONE link=$L out=$CUR"
wait
