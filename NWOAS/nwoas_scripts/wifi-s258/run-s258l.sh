#!/bin/bash
# s258l (1.0.1.3) in-place install + observation, W15 WLAN events at the end:  bash wifi-s258/run-s258l.sh <old .out> <old .link> [observe_s]
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts; D=$ROOT/wifi-s258; mkdir -p $D/runs
RES=$D/runs/s258l-$(date +%Y%m%d-%H%M%S).txt; OBS=${3:-1800}
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
T=$D/runs/boot-$(date +%H%M%S).txt
(env NWOAS_AUDIO_PROT=moderate NWOAS_DELIVER_MANIFEST=$ROOT/file-delivery-s243/manifests/wifi-j274-20261119-0328-s258l.json nohup bash $ROOT/wifi-s259/run-s259.sh $1 $2 > $T 2>&1 &)
for i in $(seq 1 240); do grep -a -q -E "S259 DONE|ABORT" $T && break; sleep 5; done
cat $T >> $RES; r=$(grep -a "S259 DONE" $T | tail -1); [ -n "$r" ] || { say "BOOT FAIL"; exit 1; }
L=$(echo "$r" | sed -n 's/.*link=\([^ ]*\).*/\1/p'); echo $L > /tmp/claude-501/curlink.txt; say "booted link=$L"
job() { python3 -I $ROOT/transport-s123/queue_job.py $L $1 >/dev/null || { say "queue fail $1"; return 1; }
  local j; j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
  for i in $(seq 1 600); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && break; sleep 3; done
  iconv -c -f CP949 -t UTF-8 $L/job-$j.log | tr -d '\r' | head -c 40000 > $D/runs/last-job.txt; cat $D/runs/last-job.txt >> $RES; }
job $ROOT/file-delivery-s243/jobs/wifi-j274-20261119-0328-s258l.cmd || exit 1
grep -a -q 'DELIVER COMPLETE' $D/runs/last-job.txt || { say "DELIVER not complete"; exit 1; }
job $D/jobs/W1-install-wifi-s258l.cmd || exit 1; say "W1 done"
grep -a -E 'DriverVersion|status=|State|상태|SSID' $D/runs/last-job.txt | grep -v BSSID | head -8 | tee -a $RES
for n in $(seq 1 $((OBS/300))); do sleep 300; job $D/jobs/W2-status.cmd; say "W2 #$n"
  grep -a -E 'NwoaEventLog|NwoaHostDisassocLog|NwoaLossLog|NwoaCmdBeforeLoss|NwoaCnt|상태|State' $D/runs/last-job.txt | cut -c1-200 | head -14 | tee -a $RES; done
job $D/jobs/W15-wlan-events.cmd; say "W15 done"
grep -a -E "W15 (COUNT3H|EV|NIC|PROF)|NwoaHostDisassocLog|NwoaCntStats" $D/runs/last-job.txt | cut -c1-200 | head -60 | tee -a $RES
say "S258L DONE link=$L"
