#!/bin/bash
# resume 2772 chain after install: bash agx-s248/run-2772-resume.sh <out> <link>
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts; R=$ROOT/agx-s248/runs; mkdir -p $R
RES=$R/chain-2772-$(date +%Y%m%d-%H%M%S).txt
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
waitline() { for i in $(seq 1 $3); do grep -a -q -E "$2" $1 && return 0; sleep 5; done; return 1; }
OUT=$1; L=$2; say "resume link=$L"
python3 -I $ROOT/transport-s123/queue_job.py $L $ROOT/agx-s248/jobs/D17b-umd-trace-off.cmd >/dev/null
j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
for i in $(seq 1 120); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && break; sleep 3; done
iconv -c -f CP949 -t UTF-8 $L/job-$j.log | tr -d '\r' | tee -a $RES
T2=$R/reboot-$(date +%H%M%S).txt
(nohup bash $ROOT/wifi-s259/run-s259.sh $OUT $L > $T2 2>&1 &)
waitline $T2 "S259 DONE|ABORT" 300; cat $T2 >> $RES
r=$(grep -a "S259 DONE" $T2 | tail -1); [ -n "$r" ] || { say "REBOOT FAIL"; exit 1; }
L=$(echo "$r" | sed -n 's/.*link=\([^ ]*\).*/\1/p'); echo $L > /tmp/claude-501/curlink.txt; say "rebooted link=$L"
sleep 60
KIT="K40 K41 K25b K21 K35g K38g K38tg K38dg K38s K41" LAUNCHER=agxdcp-s259-guest-test.sh bash $ROOT/gpucov/run-gpucov.sh run $L >> $RES 2>&1
say "CHAIN DONE link=$L gpucov=$(ls -t $ROOT/gpucov/runs/gpucov-*.txt | head -1)"
