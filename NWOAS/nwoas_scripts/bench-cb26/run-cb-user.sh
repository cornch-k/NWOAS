#!/bin/bash
# Cinebench in the user session at normal task priority on the running Windows:  bash bench-cb26/run-cb-user.sh <link>
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts; L=$1; RES=$ROOT/bench-cb26/runs/cb-user-$(date +%Y%m%d-%H%M%S).txt
job() { python3 -I $ROOT/transport-s123/queue_job.py $L $1 >/dev/null || return 1
  local j; j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
  for i in $(seq 1 200); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && break; sleep 3; done
  iconv -c -f CP949 -t UTF-8 $L/job-$j.log | tr -d '\r' >> $RES; }
job $ROOT/bench-cb26/jobs/C3n-run-interactive-normal.cmd
for i in $(seq 1 40); do sleep 30; : > $RES.c4; RES0=$RES; RES=$RES.c4; job $ROOT/bench-cb26/jobs/C4-read-results.cmd; RES=$RES0; cat $RES.c4 >> $RES; [ $(grep -a -c "^CB " $RES.c4) -ge 2 ] && break; done; rm -f $RES.c4
echo "CBUSER DONE" >> $RES; grep -a -E '^CB |===' $RES | tail -8
