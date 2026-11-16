#!/bin/bash
# Launch agxmed1g Windows (mini already at D4 proxy) + arm GPU.
set -u
NS=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
RES=$NS/agx-s248/runs/relaunch1g.txt
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
for i in $(seq 1 90); do [ -e /dev/cu.usbmodemC07HL05SQ6NY1 ] && [ -e /dev/cu.usbmodemC07HL05SQ6NY3 ] && break; sleep 2; done
sleep 6
CUR=$NS/logs/launcher-agxmed1g-relaunch-$(date "+%Y%m%d-%H%M%S").out
(caffeinate -dims bash $NS/agxmed1g-s249-guest-test.sh > $CUR 2>&1; echo "LAUNCHER EXIT $?" >> $CUR) &
say "launched $CUR"
L=""
for i in $(seq 1 240); do LG=$(sed -n "s/^Log: //p" $CUR | head -1); [ -n "$LG" ] && L=$LG.link
  [ -n "$L" ] && grep -q '"kind": 4' $L/events.jsonl 2>/dev/null && break
  grep -q "LAUNCHER EXIT" $CUR && { say "ABORT: launcher exited before worker"; tail -3 $CUR | tee -a $RES; exit 5; }
  sleep 5; done
python3 -I $NS/transport-s123/queue_job.py $L $NS/agx-s248/jobs/D28-rearm.cmd >/dev/null
j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
for i in $(seq 1 480); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && break; sleep 5; done
iconv -f CP949 -t UTF-8 $L/job-$j.log | tr -d '\r' | grep -a -E "DEV|Wom1|DWM" | tee -a $RES
say "ARMED DONE link=$L out=$CUR"
wait
