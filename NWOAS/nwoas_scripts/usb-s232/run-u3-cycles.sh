#!/bin/bash
# S232 USB-A validation: N cycles of (queue U3 copy job -> record result -> Windows restart -> relaunch S232 -> wait worker).
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
CYCLES=${1:-3}
RES=$ROOT/usb-s232/u3-results.txt
cur=$(ls -t $ROOT/logs/launcher-s232-*.out | head -1)
for c in $(seq 1 $CYCLES); do
  L=$(sed -n 's/^Log: //p' $cur).link
  for i in $(seq 1 180); do grep -q '"kind": 4' $L/events.jsonl 2>/dev/null && break; grep -q "LAUNCHER EXIT" $cur && { echo "cycle $c: session ended before worker"; exit 2; }; sleep 5; done
  last=$(python3 -I -c "import json,sys;e=[json.loads(x) for x in open(sys.argv[1])];print(max([x['job'] for x in e]))" $L/events.jsonl)
  python3 -I $ROOT/transport-s123/queue_job.py $L $ROOT/usb-s232/U3-fresh-copy.cmd || exit 3
  j=$((last+1)); [ -f $L/job.json ] && j=$(python3 -I -c "import json;print(json.load(open('$L/job.json'))['id'])")
  for i in $(seq 1 240); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && break; sleep 5; done
  r=$(tr -d '\r' < $L/job-$j.log | grep -a -E "^SHA256|RESULT|NO USB" | tr '\n' ' ')
  echo "cycle $c $(date '+%H:%M:%S') $r" | tee -a $RES
  [ $c -eq $CYCLES ] && break
  python3 -I $ROOT/transport-s123/queue_job.py $L $ROOT/layout-s231/jobs/W4-restart.cmd || exit 4
  for i in $(seq 1 150); do grep -q "LAUNCHER EXIT" $cur && break; sleep 2; done
  for i in $(seq 1 60); do [ -e /dev/cu.usbmodemC07HL05SQ6NY1 ] && [ -e /dev/cu.usbmodemC07HL05SQ6NY3 ] && break; sleep 2; done
  sleep 4
  cur=$ROOT/logs/launcher-s232-$(date '+%Y%m%d-%H%M%S')-c$c.out
  (caffeinate -dims bash $ROOT/usb-s232-guest-test.sh > $cur 2>&1; echo "LAUNCHER EXIT $?" >> $cur) &
  sleep 10
done
echo "U3 CYCLES DONE"
# keep the last launched guest session attached to this process
wait
