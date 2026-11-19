#!/bin/bash
# H5 (fw roaming on) then H7 (interrupt mode), 10 min each with driver test scans every 120 s:  bash wifi-s258/run-w28.sh <link>
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts; D=$ROOT/wifi-s258; J=$D/jobs; L=$1
RES=$D/runs/w28-$(date +%Y%m%d-%H%M%S).txt
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
job() { python3 -I $ROOT/transport-s123/queue_job.py $L $1 >/dev/null || { say "queue fail $1"; return 1; }
  local j; j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
  for i in $(seq 1 300); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && break; sleep 3; done
  iconv -c -f CP949 -t UTF-8 $L/job-$j.log | tr -d '\r' > $D/runs/last-job.txt; cat $D/runs/last-job.txt >> $RES; }
for m in W28-roamon W28-irq; do job $J/$m.cmd; say "$m set"; sleep 620; job $J/W2-status.cmd
  say "$m RESULT $(grep -a -E 'NwoaScanFlow ' $D/runs/last-job.txt | tr -s ' ' | cut -c1-220) | isr=$(grep -a 'NwoaCntIsr ' $D/runs/last-job.txt | awk '{print $3}') | host=$(grep -a 'NwoaHostDisassocLog' $D/runs/last-job.txt | tr -s ' ' | cut -c1-80)"; done
job $J/W28-restore.cmd; say "restored"; say "W28 DONE link=$L"
