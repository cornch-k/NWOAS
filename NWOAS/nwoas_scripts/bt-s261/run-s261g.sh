#!/bin/bash
# restart into the S261 (Bluetooth) launcher with the s261g delivery, then install and test s261g
#   bash bt-s261/run-s261g.sh <old .out> <old .link>
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts; J=$ROOT/bt-s261/jobs; R=$ROOT/bt-s261/runs; mkdir -p $R
RES=$R/s261g-$(date +%Y%m%d-%H%M%S).txt
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
T=$R/boot-$(date +%H%M%S).txt
(env NWOAS_AUDIO_PROT=moderate NWOAS_DELIVER_MANIFEST=$ROOT/file-delivery-s243/manifests/bt-j274-20261118-1849-s261g.json nohup bash $ROOT/bt-s261/run-s261.sh $1 $2 > $T 2>&1 &)
for i in $(seq 1 240); do grep -a -q -E "S261 DONE|ABORT" $T && break; sleep 5; done
cat $T >> $RES; r=$(grep -a "S261 DONE" $T | tail -1); [ -n "$r" ] || { say "BOOT FAIL"; exit 1; }
L=$(echo "$r" | sed -n 's/.*link=\([^ ]*\).*/\1/p'); echo $L > /tmp/claude-501/curlink.txt; say "booted link=$L"
job() { python3 -I $ROOT/transport-s123/queue_job.py $L $1 >/dev/null || { say "queue fail $1"; return 1; }
  local j; j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
  for i in $(seq 1 120); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && break; sleep 3; done
  grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl || { say "TIMEOUT $1 (Windows may be frozen)"; return 1; }
  iconv -c -f CP949 -t UTF-8 $L/job-$j.log | tr -d '\r' >> $RES; iconv -c -f CP949 -t UTF-8 $L/job-$j.log | tr -d '\r' | grep -a -iE 'DONE|FAIL|status=|problem|Code|RADIO|EXIT' | head -10 | cut -c1-200; }
for jb in $J/B9-uninstall-bt.cmd $ROOT/file-delivery-s243/jobs/bt-j274-20261118-1849-s261g.cmd $J/B1-install-bt-s261g.cmd $J/B5-stopafter-0.cmd $J/B10-ioctl-trace.cmd $J/B2-status.cmd $J/B7-wifi-coexist.cmd $J/B2-status.cmd; do
  say "== $(basename $jb)"; job $jb || { say "STOP at $(basename $jb)"; exit 3; }; done
say "S261G DONE link=$L"
