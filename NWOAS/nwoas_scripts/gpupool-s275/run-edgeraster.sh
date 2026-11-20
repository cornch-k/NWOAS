#!/bin/bash
# K52 wrapper: daily S268 boot (bt-s268/run-s268.sh, Bluetooth + SSD default) with edgeraster1.ps1 + raster1.html in
# the delivery manifest -> gpucov19 (deliver both to C:\NWOAS\gpucov) -> K52 (Edge default vs
# --enable-gpu-rasterization on the same static page: 4 captures each, 60 s scroll+zoom stress, capture again,
# block compare). No driver install, no registry change. Same flow as gpupool-s272/run-edgegpu.sh (run 11-20 10:23).
#   bash gpupool-s275/run-edgeraster.sh <current launcher .out> <current .link>     (or: idle -)
# Pass: last K52 line "K52 VERDICT ... => PASS", then "EDGERASTER DONE".
# Not run by the agent that wrote it (HARDWARE-UNVERIFIED).
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts; R=$ROOT/gpupool-s275/runs; mkdir -p $R /tmp/claude-501
RES=$R/edgeraster-$(date +%Y%m%d-%H%M%S).txt
MANF=$ROOT/file-delivery-s243/manifests/gpucov-edgeraster.json
DJOB=$ROOT/file-delivery-s243/jobs/gpucov19-edgeraster.cmd
K52=$ROOT/gpucov/jobs/K52-edge-raster-ab.cmd
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
for f in $MANF $DJOB $K52; do [ -f $f ] || { echo "STOP: missing $f"; exit 1; }; done
job() { local L=$1 f=$2 j
  say "== $(basename $f)"
  python3 -I $ROOT/transport-s123/queue_job.py $L $f >/dev/null || { say "ABORT queue $(basename $f)"; return 3; }
  j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
  for i in $(seq 1 240); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && break; sleep 5; done
  JOBLOG=$L/job-$j.log; iconv -c -f CP949 -t UTF-8 $JOBLOG | tr -d '\r' | head -c 200000 | tee -a $RES; }
T=$R/s268-$(date +%H%M%S).txt
(NWOAS_DELIVER_MANIFEST=$MANF nohup bash $ROOT/bt-s268/run-s268.sh $1 $2 > $T 2>&1 &)
for i in $(seq 1 400); do grep -a -q -E "S268 DONE link=|ABORT" $T && break; sleep 5; done
cat $T >> $RES
BL=$(grep -a "S268 DONE link=" $T | tail -1); [ -n "$BL" ] || { say "BOOT FAIL ($T)"; exit 1; }
L=$(echo "$BL" | sed -n 's/.*link=\([^ ]*\).*/\1/p'); OUT=$(echo "$BL" | sed -n 's/.* out=\([^ ]*\).*/\1/p')
echo $L > /tmp/claude-501/curlink.txt; say "booted link=$L out=$OUT"
job $L $DJOB || exit $?
grep -a -q "S243 DELIVER COMPLETE" $JOBLOG || { say "ABORT edgeraster delivery"; exit 5; }
sleep 30
job $L $K52 || exit $?
say "EDGERASTER DONE link=$L out=$OUT res=$RES"
