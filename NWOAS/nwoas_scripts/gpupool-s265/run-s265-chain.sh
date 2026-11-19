#!/bin/bash
# S265 chain (GPU pool window, no driver change): boot-s265 (pool on + probe11 delivery + D28) -> K43 memory
# kit + regressions -> W4 restart (the shutdown is the KMD stop path through the pool window) -> boot-s265 again
# -> K43b (pool came back) + K41 -> "S265 CHAIN DONE".
#   bash gpupool-s265/run-s265-chain.sh <current launcher .out> <current .link>     (or: idle -)
#   KIT="K43 K41" to shorten the first kit.  NWOAS_S262_HV=1 etc. pass through to the launcher unchanged.
# Each boot wait matches "S265 BOOT DONE link=" only (that exact text never appears inside a job log).
# Rollback: bash wifi-s259/run-s259.sh <.out> <.link>  (S259 launcher = pool off; nothing on Windows to undo).
# Not run by the agent that wrote it (HARDWARE-UNVERIFIED).
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts; R=$ROOT/gpupool-s265/runs; mkdir -p $R
RES=$R/chain-s265-$(date +%Y%m%d-%H%M%S).txt
MANF=$ROOT/file-delivery-s243/manifests/gpupool-s265-probe11.json
DJOBF=$ROOT/file-delivery-s243/jobs/gpupool-s265-probe11.cmd
KIT=${KIT:-"K43 K41 K9 K10 K25b K21 K38g K38dg K41"}
KIT2=${KIT2:-"K43b K41"}
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
waitline() { for i in $(seq 1 $3); do grep -a -q -E "$2" $1 && return 0; sleep 5; done; return 1; }
kit() { local L=$1 LOG=$2 OUT=$3 k f j; shift 3
  for k in "$@"; do f=$(ls $ROOT/gpucov/jobs/$k-*.cmd 2>/dev/null | head -1); [ -n "$f" ] || { say "no job $k"; continue; }
    say "== $k $(basename $f)"
    python3 -I $ROOT/transport-s123/queue_job.py $L $f >/dev/null || { say "ABORT queue $k"; return 3; }
    j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
    for i in $(seq 1 480); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && break
      if grep -a -q -E 'Exception taken from EL2|Unhandled exception' "$LOG.vuart" || grep -q "LAUNCHER EXIT" $OUT; then
        say "ABORT hypervisor/launcher gone during $k"; grep -a -n -B3 -A12 'Exception taken from EL2' "$LOG.vuart" | head -40 | tee -a $RES; return 7; fi
      sleep 5; done
    iconv -c -f CP949 -t UTF-8 $L/job-$j.log | tr -d '\r' | head -c 400000 | tee -a $RES
  done; }
boot() { local T=$R/boot-$(date +%H%M%S).txt
  (MAN=${3:-} DJOB=${4:-} nohup bash $ROOT/gpupool-s265/boot-s265.sh $1 $2 > $T 2>&1 &)
  waitline $T "S265 BOOT DONE link=|ABORT" 400; cat $T >> $RES
  BL=$(grep -a "S265 BOOT DONE link=" $T | tail -1); [ -n "$BL" ] || { say "BOOT FAIL ($T)"; exit 1; }
  L=$(echo "$BL" | sed -n 's/.*link=\([^ ]*\).*/\1/p'); OUT=$(echo "$BL" | sed -n 's/.* out=\([^ ]*\).*/\1/p')
  LOG=$(echo "$BL" | sed -n 's/.* log=\([^ ]*\).*/\1/p'); POOL=$(echo "$BL" | sed -n 's/.* pool=\([01]\).*/\1/p')
  echo $L > /tmp/claude-501/curlink.txt; say "booted link=$L pool=$POOL out=$OUT"; }
boot $1 $2 $MANF $DJOBF
[ "$POOL" = 1 ] || { say "POOL NOT ARMED (hypervisor lines above). Windows runs pool-off on S265; stop here"; exit 8; }
sleep 60
kit $L $LOG $OUT $KIT || exit $?
say "restart: second pool boot (the shutdown runs the KMD stop path through the pool window)"
boot $OUT $L
kit $L $LOG $OUT $KIT2 || exit $?
say "S265 CHAIN DONE link=$L out=$OUT pool=$POOL res=$RES"
