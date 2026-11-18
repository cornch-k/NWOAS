#!/bin/bash
# P35 (2771): restart current session -> $LAUNCHER (default agxdcp-s256) with the 2771 delivery manifest -> deliver -> install (I33).
# Usage: bash agx-s248/run-install-2771-norestart.sh   (mini already at the proxy screen)   [LAUNCHER=agxdcp-s2NN-guest-test.sh]
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
RES=$ROOT/agx-s248/runs/install-2771.txt
LAUNCHER=${LAUNCHER:-agxdcp-s256-guest-test.sh}
LTAG=$(echo $LAUNCHER | sed -n "s/^agxdcp-\(s[0-9]*\).*/\1/p")
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
CUR=$ROOT/logs/launcher-agxdcp${LTAG#s}-2771-$(date "+%Y%m%d-%H%M%S").out
(NWOAS_DELIVER_MANIFEST=$ROOT/file-delivery-s243/manifests/agx-j274-2771.json caffeinate -dims bash $ROOT/$LAUNCHER > $CUR 2>&1; echo "LAUNCHER EXIT $?" >> $CUR) &
say "launched $CUR"
L=""
for i in $(seq 1 240); do LOG=$(sed -n "s/^Log: //p" $CUR | head -1); [ -n "$LOG" ] && L=$LOG.link
  [ -n "$L" ] && grep -q '"kind": 4' $L/events.jsonl 2>/dev/null && break
  grep -q "LAUNCHER EXIT" $CUR && { say "ABORT: launcher exited before worker"; tail -20 $CUR | tee -a $RES; exit 2; }
  sleep 5; done
grep -q '"kind": 4' $L/events.jsonl 2>/dev/null || { say "ABORT worker timeout"; exit 2; }
say "worker connected link=$L"
python3 -I $ROOT/transport-s123/queue_job.py $L $ROOT/file-delivery-s243/jobs/agx-j274-2771-pkg.cmd >/dev/null; jobwait $L
say "deliver: $(iconv -f CP949 -t UTF-8 $JOBLOG | tr -d '\r' | grep -a -E 'READBACK|EXIT|FAIL' | tr '\n' ' ')"
grep -a -q "READBACK MATCH" $JOBLOG || { say "ABORT delivery"; wait; exit 5; }
python3 -I $ROOT/transport-s123/queue_job.py $L $ROOT/agx-s248/jobs/I33-install-2771.cmd >/dev/null; jobwait $L
iconv -f CP949 -t UTF-8 $JOBLOG | tr -d '\r' | tee -a $RES
say "I33 DONE link=$L"
wait
