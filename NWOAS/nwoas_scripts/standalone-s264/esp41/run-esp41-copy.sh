#!/bin/bash
# PLAN-4-1 6장 4번: daily S269 boot with the esp-4-1 manifest -> deliver 6 files to C:\NWOAS\drop\esp-4-1 -> copy to ESP \m1n1 with readback.
#   bash run-esp41-copy.sh <current launcher .out> <current .link>
#   bash run-esp41-copy.sh link <boot .txt>   (already booted with the esp-4-1 manifest)
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts; R=$ROOT/standalone-s264/esp41/runs; mkdir -p $R
RES=$R/esp41-$(date +%Y%m%d-%H%M%S).txt
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
T=$R/boot-$(date +%H%M%S).txt
if [ "$1" != link ]; then
  # the wrapper never exits while Windows runs: start it detached and judge by its DONE line (never kill it)
  (NWOAS_DELIVER_MANIFEST=$ROOT/file-delivery-s243/manifests/esp-4-1.json nohup bash $ROOT/bt-s268/run-s269.sh $1 $2 > $T 2>&1 &)
  for i in $(seq 1 240); do grep -a -q -E "S269 DONE|ABORT" $T && break; sleep 5; done
else T=$2; fi   # bash run-esp41-copy.sh link <boot .txt with the S269 DONE line of a manifest boot>
D=$(grep -a "S269 DONE" $T | tail -1); say "boot: $D"
[ -n "$D" ] || { say "ABORT boot"; tail -20 $T >> $RES; exit 2; }
L=$(echo "$D" | sed -E 's/.*link=([^ ]+).*/\1/')
jobwait() { local j; j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
  for i in $(seq 1 360); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && break; sleep 5; done
  JOBLOG=$L/job-$j.log; }
python3 -I $ROOT/transport-s123/queue_job.py $L $ROOT/file-delivery-s243/jobs/esp-4-1-deliver.cmd >/dev/null || { say "ABORT queue deliver"; exit 3; }
jobwait; iconv -c -f CP949 -t UTF-8 $JOBLOG | tr -d '\r' >> $RES
grep -a -q "S243 DELIVER COMPLETE" $JOBLOG || { say "ABORT deliver"; exit 4; }
python3 -I $ROOT/transport-s123/queue_job.py $L $ROOT/standalone-s264/stage1/J-esp-4-1-copy.cmd >/dev/null || { say "ABORT queue copy"; exit 3; }
jobwait; iconv -c -f CP949 -t UTF-8 $JOBLOG | tr -d '\r' >> $RES
grep -a -q "NWOAS ESP-4-1 COPY COMPLETE" $JOBLOG || { say "ESP41 FAIL"; exit 5; }
say "ESP41 DONE $D"
