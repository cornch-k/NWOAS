#!/bin/bash
# Cinebench 2026 multi+single on a boot with S260 core-type table (MADT) + WFI + second tick.
#   bash bench-cb26/run-cb-madt.sh <old .out> <old .link>
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts; R=$ROOT/bench-cb26/runs; mkdir -p $R
RES=$R/cb-madt-$(date +%Y%m%d-%H%M%S).txt
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
T=$R/boot-$(date +%H%M%S).txt
(env NWOAS_AUDIO_PROT=moderate NWOAS_S260_HV=1 NWOAS_S260_MADT=1 NWOAS_PWR_WFI=1 NWOAS_PWR_SECTICK=100 nohup bash $ROOT/wifi-s259/run-s259.sh $1 $2 > $T 2>&1 &)
for i in $(seq 1 240); do grep -a -q -E "S259 DONE|ABORT" $T && break; sleep 5; done
cat $T >> $RES; r=$(grep -a "S259 DONE" $T | tail -1); [ -n "$r" ] || { say "BOOT FAIL"; exit 1; }
L=$(echo "$r" | sed -n 's/.*link=\([^ ]*\).*/\1/p'); LOG=$(echo "$r" | sed -n 's/.* log=\([^ ]*\).*/\1/p'); say "booted link=$L"
grep -a -E '\[S260\]' $LOG | head -5 | tee -a $RES
sleep 90
python3 -I $ROOT/transport-s123/queue_job.py $L $ROOT/bench-cb26/jobs/C2-run-cinebench.cmd >/dev/null || { say "queue fail"; exit 1; }
j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
for i in $(seq 1 300); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && break; sleep 5; done
iconv -c -f CP949 -t UTF-8 $L/job-$j.log | tr -d '\r' | tee -a $RES | grep -a -iE 'EXIT|CB |score|pts|===' | head -20
say "CB DONE link=$L $r"
