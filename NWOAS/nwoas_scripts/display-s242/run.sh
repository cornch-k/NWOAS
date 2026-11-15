#!/bin/bash
# S242: restart current session -> launch display-s242 (S237 stack + 1080p-first hook) -> D1 display query.
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
RES=$ROOT/display-s242/results.txt
W4=$ROOT/layout-s231/jobs/W4-restart.cmd
CUR=$(ls -t $ROOT/logs/launcher-*.out | head -1)
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
linkdir() { echo "$(sed -n 's/^Log: //p' $CUR).link"; }
wait_worker() { local L; for i in $(seq 1 240); do L=$(linkdir)
  grep -q '"kind": 4' $L/events.jsonl 2>/dev/null && return 0
  grep -q "LAUNCHER EXIT" $CUR && { say "ABORT: session ended before worker ($CUR)"; exit 2; }
  sleep 5; done; say "ABORT: worker timeout"; exit 2; }
wait_idle() { local L=$(linkdir) j; [ -f $L/job.json ] || return 0
  j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
  for i in $(seq 1 480); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && return 0; sleep 5; done
  say "ABORT: previous job $j never completed"; exit 3; }
run_job() { local L j; wait_worker; wait_idle; L=$(linkdir)
  python3 -I $ROOT/transport-s123/queue_job.py $L $1 >/dev/null || { say "ABORT: queue $1"; exit 3; }
  j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
  for i in $(seq 1 480); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && break; sleep 5; done
  JOBOUT=$(iconv -f CP949 -t UTF-8 $L/job-$j.log 2>/dev/null | tr -d '\r' | grep -a -E "$2" | tr '\n' ' '); }
say "START cur=$CUR"
run_job $W4 "RESTART"; say "restart: $JOBOUT"
for i in $(seq 1 200); do grep -q "LAUNCHER EXIT" $CUR && break; sleep 2; done
grep -q "LAUNCHER EXIT" $CUR || { say "ABORT: launcher did not exit"; exit 4; }
for i in $(seq 1 90); do [ -e /dev/cu.usbmodemC07HL05SQ6NY1 ] && [ -e /dev/cu.usbmodemC07HL05SQ6NY3 ] && break; sleep 2; done
sleep 4
CUR=$ROOT/logs/launcher-s242-$(date "+%Y%m%d-%H%M%S").out
(caffeinate -dims bash $ROOT/display-s242-guest-test.sh > $CUR 2>&1; echo "LAUNCHER EXIT $?" >> $CUR) &
sleep 10
run_job $ROOT/display-s242/D1-display.cmd "Name|Status|Current|VideoMode|Instance|Max|COMPLETE"
say "D1: $JOBOUT"
L=$(sed -n 's/^Log: //p' $CUR); say "log=$L"
grep -a -i -E "1920|1280|timing|mode|swap|FB_" $L $L.dcp 2>/dev/null | grep -a -v "^Binary" | tail -15 | cut -c1-200 | tee -a $RES
say "S242 DONE"
wait
