#!/bin/bash
# S248: restart current agxmed session -> agxmed-s247 with the 2757 delivery manifest -> deliver -> install (I19).
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
RES=$ROOT/agx-s248/runs/install-2757.txt
W4=$ROOT/layout-s231/jobs/W4-restart.cmd
OLDOUT=; OLDLINK=
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
jobwait() { local L=$1 j; j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
  for i in $(seq 1 480); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && break; sleep 5; done
  JOBLOG=$L/job-$j.log; }
say "START old=$OLDLINK"
say "no restart: mini already at proxy"
for i in $(seq 1 90); do [ -e /dev/cu.usbmodemC07HL05SQ6NY1 ] && [ -e /dev/cu.usbmodemC07HL05SQ6NY3 ] && break; sleep 2; done
sleep 4
CUR=$ROOT/logs/launcher-agxdcp253-2757-$(date "+%Y%m%d-%H%M%S").out
(NWOAS_DELIVER_MANIFEST=$ROOT/file-delivery-s243/manifests/agx-j274-2757.json caffeinate -dims bash $ROOT/agxdcp-s253-guest-test.sh > $CUR 2>&1; echo "LAUNCHER EXIT $?" >> $CUR) &
say "launched $CUR"
L=""
for i in $(seq 1 240); do LOG=$(sed -n "s/^Log: //p" $CUR | head -1); [ -n "$LOG" ] && L=$LOG.link
  [ -n "$L" ] && grep -q '"kind": 4' $L/events.jsonl 2>/dev/null && break
  grep -q "LAUNCHER EXIT" $CUR && { say "ABORT: launcher exited before worker"; tail -20 $CUR | tee -a $RES; exit 2; }
  sleep 5; done
grep -q '"kind": 4' $L/events.jsonl 2>/dev/null || { say "ABORT worker timeout"; exit 2; }
say "worker connected link=$L"
python3 -I $ROOT/transport-s123/queue_job.py $L $ROOT/file-delivery-s243/jobs/agx-j274-2757.cmd >/dev/null; jobwait $L
say "deliver: $(iconv -f CP949 -t UTF-8 $JOBLOG | tr -d '\r' | grep -a -E 'READBACK|EXIT|FAIL' | tr '\n' ' ')"
grep -a -q "READBACK MATCH" $JOBLOG || { say "ABORT delivery"; wait; exit 5; }
python3 -I $ROOT/transport-s123/queue_job.py $L $ROOT/agx-s248/jobs/I19-install-2757.cmd >/dev/null; jobwait $L
iconv -f CP949 -t UTF-8 $JOBLOG | tr -d '\r' | tee -a $RES
say "I19 DONE link=$L"
wait
