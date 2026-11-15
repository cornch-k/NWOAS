#!/bin/bash
# S234: restart Windows out of the current session, launch <LAUNCHER>, then run N U3 fresh-boot cycles.
# usage: switch-and-run.sh <tag> <launcher.sh> <cycles> [job-before-restart.cmd]
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
TAG=$1; LAUNCHER=$2; CYCLES=$3; PRE=${4:-}
cur=$(ls -t $ROOT/logs/launcher-*.out | head -1)
L=$(sed -n 's/^Log: //p' $cur).link
if ! grep -q "LAUNCHER EXIT" $cur; then
  if [ -n "$PRE" ]; then
    last=$(python3 -I -c "import json,sys;e=[json.loads(x) for x in open(sys.argv[1])];print(max([x['job'] for x in e]))" $L/events.jsonl)
    python3 -I $ROOT/transport-s123/queue_job.py $L $PRE || exit 3
    j=$(python3 -I -c "import json;print(json.load(open('$L/job.json'))['id'])")
    for i in $(seq 1 120); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && break; sleep 2; done
    echo "pre job $j:"; tr -d '\r' < $L/job-$j.log | tail -8
  fi
  python3 -I $ROOT/transport-s123/queue_job.py $L $ROOT/layout-s231/jobs/W4-restart.cmd || exit 4
  for i in $(seq 1 150); do grep -q "LAUNCHER EXIT" $cur && break; sleep 2; done
fi
for i in $(seq 1 60); do [ -e /dev/cu.usbmodemC07HL05SQ6NY1 ] && [ -e /dev/cu.usbmodemC07HL05SQ6NY3 ] && break; sleep 2; done
sleep 4
new=$ROOT/logs/launcher-$TAG-$(date "+%Y%m%d-%H%M%S")-c0.out
(caffeinate -dims bash $ROOT/$LAUNCHER > $new 2>&1; echo "LAUNCHER EXIT $?" >> $new) &
sleep 10
echo "launched $new"
LAUNCHER=$LAUNCHER TAG=$TAG exec bash $ROOT/usb-s232/run-u3-cycles2.sh $CYCLES
