#!/bin/bash
# S267 chain after the 2774 K43c result (memladder2 2304/2560 FAIL at probe_readback_map, 1792/2048/3072 PASS).
# NO new driver: 2774 (P38) stays installed. Mirrors gpupool-s266/run-2774-chain.sh step 2 only:
#   boot = boot-s265.sh, pool ON, delivering probe13 during the boot (MAN/DJOB) -> D28
#   -> K43t (read-only: the failure lines of the 08:10 K43c UMD trace C:\NWOAS\gpucov\k43c-umd.log + probe13 rcpt)
#   -> K43d (memladder order test: 2304 alone / 2048 then 2304 / settle=15000 ladder) -> K41
#   bash gpupool-s267/run-k43d-chain.sh <current launcher .out> <current .link>     (or: idle -)
#   KIT overrides the job list. NWOAS_S262_HV=1 etc. pass through to the launcher unchanged.
# Every boot wait matches "S265 BOOT DONE link=" only (that exact text never appears inside a job log).
# Stops if the installed driver is not 30.0.2774.0. Rollback is not needed (nothing is installed);
# 2773: agx-s248/jobs/R36-rollback-2773.cmd as in RESULT-2774.md section 6.
# Not run by the agent that wrote it (HARDWARE-UNVERIFIED).
echo "STOP: superseded by gpupool-s267/run-k43e-chain.sh (K43d-ladder-order-p13.cmd is 5,990 B, over the 4,032 B job mailbox)"; exit 1
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts; R=$ROOT/gpupool-s267/runs; mkdir -p $R
RES=$R/chain-k43d-$(date +%Y%m%d-%H%M%S).txt
MANF=$ROOT/file-delivery-s243/manifests/gpucov-probe13.json
KITJ=$ROOT/file-delivery-s243/jobs/gpucov15-probe13.cmd
KIT=${KIT:-"K43t K43d K41"}
for f in $MANF $KITJ $ROOT/gpucov/jobs/K43t-umdtrace-2774.cmd $ROOT/gpucov/jobs/K43d-ladder-order-p13.cmd; do
  [ -f $f ] || { echo "STOP: missing $f (python3 -I file-delivery-s243/make-probe13.py)"; exit 1; }; done
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
waitline() { for i in $(seq 1 $3); do grep -a -q -E "$2" $1 && return 0; sleep 5; done; return 1; }
job() { local L=$1 LOG=$2 OUT=$3 f=$4 j  # one job file by path; same EL2 / launcher-exit watch as kit()
  say "== $(basename $f)"
  python3 -I $ROOT/transport-s123/queue_job.py $L $f >/dev/null || { say "ABORT queue $(basename $f)"; return 3; }
  j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
  for i in $(seq 1 480); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && break
    if grep -a -q -E 'Exception taken from EL2|Unhandled exception' "$LOG.vuart" || grep -q "LAUNCHER EXIT" $OUT; then
      say "ABORT hypervisor/launcher gone during $(basename $f)"; grep -a -n -B3 -A12 'Exception taken from EL2' "$LOG.vuart" | head -40 | tee -a $RES; return 7; fi
    sleep 5; done
  JOBLOG=$L/job-$j.log
  iconv -c -f CP949 -t UTF-8 $JOBLOG | tr -d '\r' | head -c 400000 | tee -a $RES; }
kit() { local L=$1 LOG=$2 OUT=$3 k f; shift 3
  for k in "$@"; do f=$(ls $ROOT/gpucov/jobs/$k-*.cmd 2>/dev/null | head -1); [ -n "$f" ] || { say "no job $k"; continue; }
    job $L $LOG $OUT $f || return $?
    if [ $k = K43t ] && ! grep -a -q "K43t VC .*30.0.2774.0" $JOBLOG; then say "STOP: installed driver is not 30.0.2774.0"; return 9; fi
  done; }
boot() { local T=$R/boot-$(date +%H%M%S).txt
  (NWOAS_S265_POOL=${5:-${NWOAS_S265_POOL:-1}} MAN=${3:-} DJOB=${4:-} nohup bash $ROOT/gpupool-s265/boot-s265.sh $1 $2 > $T 2>&1 &)
  waitline $T "S265 BOOT DONE link=|ABORT" 400; cat $T >> $RES
  BL=$(grep -a "S265 BOOT DONE link=" $T | tail -1); [ -n "$BL" ] || { say "BOOT FAIL ($T)"; exit 1; }
  L=$(echo "$BL" | sed -n 's/.*link=\([^ ]*\).*/\1/p'); OUT=$(echo "$BL" | sed -n 's/.* out=\([^ ]*\).*/\1/p')
  LOG=$(echo "$BL" | sed -n 's/.* log=\([^ ]*\).*/\1/p'); POOL=$(echo "$BL" | sed -n 's/.* pool=\([01]\).*/\1/p')
  echo $L > /tmp/claude-501/curlink.txt; say "booted link=$L pool=$POOL out=$OUT"; }
say "START k43d chain (2774 stays installed) old=$2"
boot $1 $2 $MANF $KITJ 1
[ "$POOL" = 1 ] || { say "POOL NOT ARMED (hypervisor lines above); stop here"; exit 8; }
grep -a -q "S243 DELIVER COMPLETE" $RES || { say "ABORT probe13 delivery (no S243 DELIVER COMPLETE in the boot log)"; exit 5; }
sleep 60
kit $L $LOG $OUT $KIT || exit $?
say "S267 CHAIN DONE link=$L out=$OUT pool=$POOL res=$RES"
