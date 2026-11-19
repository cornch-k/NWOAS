#!/bin/bash
# H1 test on the running Windows (s258p installed): ScanTxHold=1 + driver test scan every 120 s, ping, 20 min.
#   bash wifi-s258/run-txhold.sh <link>
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts; D=$ROOT/wifi-s258; J=$D/jobs; L=$1
RES=$D/runs/txhold-$(date +%Y%m%d-%H%M%S).txt
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
job() { python3 -I $ROOT/transport-s123/queue_job.py $L $1 >/dev/null || { say "queue fail $1"; return 1; }
  local j; j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
  for i in $(seq 1 300); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && break; sleep 3; done
  iconv -c -f CP949 -t UTF-8 $L/job-$j.log | tr -d '\r' > $D/runs/last-job.txt; cat $D/runs/last-job.txt >> $RES; }
job $J/W27-txhold-on.cmd; say "W27 on"
job $J/W19-ping-start-s258p.cmd; say "W19 start"
for n in 1 2 3 4; do sleep 300; job $J/W2-status.cmd; say "W2 #$n $(grep -a -E 'NwoaScanFlow ' $D/runs/last-job.txt | tr -s ' ' | cut -c1-200)"; done
job $J/W19-ping-stop.cmd; say "W19 stop $(grep -a 'W19 END' $D/runs/last-job.txt)"
job $J/W2-status.cmd; grep -a -E 'NwoaScanFlow|NwoaHostDisassocLog|NwoaCntTxStall|NwoaStallLog' $D/runs/last-job.txt | cut -c1-500 | tee -a $RES
job $J/W27-txhold-off.cmd; say "W27 off"
say "TXHOLD DONE link=$L"
