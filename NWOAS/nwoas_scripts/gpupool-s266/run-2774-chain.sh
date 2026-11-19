#!/bin/bash
# S266 / P38 chain (driver 2774 on the S265 pool window): mirrors gpupool-s265/run-s265-chain.sh.
#   1. boot A = boot-s265.sh with NWOAS_S265_POOL=0 (pool OFF: the same install conditions as 2773) and the 2774
#      manifest; delivers the package (DJOB) -> D28; then I36 install 2774 + probe12 delivery (gpucov14).
#   2. boot B = boot-s265.sh, pool ON (W4 restart: 2774 starts cleanly with the pool) -> K43c + regressions.
#   3. boot C = boot-s265.sh, pool ON again (the shutdown is the KMD stop path through the pool) -> K43b + K41.
#   bash gpupool-s266/run-2774-chain.sh <current launcher .out> <current .link>     (or: idle -)
#   KIT / KIT2 override the kits. NWOAS_S262_HV=1 etc. pass through to the launcher unchanged.
# Every boot wait matches "S265 BOOT DONE link=" only (that exact text never appears inside a job log).
# Rollback: agx-s248/jobs/R36-rollback-2773.cmd on any running boot, then W4 (RESULT-2774.md section 6).
# Not run by the agent that wrote it (HARDWARE-UNVERIFIED).
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts; R=$ROOT/gpupool-s266/runs; mkdir -p $R
RES=$R/chain-2774-$(date +%Y%m%d-%H%M%S).txt
MANF=$ROOT/file-delivery-s243/manifests/agx-j274-2774.json
PKGJ=$ROOT/file-delivery-s243/jobs/agx-j274-2774-pkg.cmd
KITJ=$ROOT/file-delivery-s243/jobs/gpucov14-j274-2774.cmd
INST=$ROOT/agx-s248/jobs/I36-install-2774.cmd
KIT=${KIT:-"K43c K41 K9 K10 K25b K21 K38g K38dg K41"}
KIT2=${KIT2:-"K43b K41"}
for f in $MANF $PKGJ $KITJ $INST $ROOT/gpucov/jobs/K43c-pool-mem-p38.cmd; do
  [ -f $f ] || { echo "STOP: missing $f (python3 -I file-delivery-s243/make-2774.py <build-p38 out dir>)"; exit 1; }; done
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
    job $L $LOG $OUT $f || return $?; done; }
boot() { local T=$R/boot-$(date +%H%M%S).txt
  (NWOAS_S265_POOL=${5:-${NWOAS_S265_POOL:-1}} MAN=${3:-} DJOB=${4:-} nohup bash $ROOT/gpupool-s265/boot-s265.sh $1 $2 > $T 2>&1 &)
  waitline $T "S265 BOOT DONE link=|ABORT" 400; cat $T >> $RES
  BL=$(grep -a "S265 BOOT DONE link=" $T | tail -1); [ -n "$BL" ] || { say "BOOT FAIL ($T)"; exit 1; }
  L=$(echo "$BL" | sed -n 's/.*link=\([^ ]*\).*/\1/p'); OUT=$(echo "$BL" | sed -n 's/.* out=\([^ ]*\).*/\1/p')
  LOG=$(echo "$BL" | sed -n 's/.* log=\([^ ]*\).*/\1/p'); POOL=$(echo "$BL" | sed -n 's/.* pool=\([01]\).*/\1/p')
  echo $L > /tmp/claude-501/curlink.txt; say "booted link=$L pool=$POOL out=$OUT"; }
say "START 2774 chain old=$2"
# 1. install under pool OFF (boot-s265.sh and the S265 launcher honour NWOAS_S265_POOL; 5th boot argument)
boot $1 $2 $MANF $PKGJ 0
job $L $LOG $OUT $INST || exit $?
grep -a -q "30.0.2774.0" $JOBLOG || { say "INSTALL FAIL: VC is not 30.0.2774.0 (rollback: R36)"; exit 9; }
job $L $LOG $OUT $KITJ || exit $?
grep -a -q "S243 DELIVER COMPLETE" $JOBLOG || { say "ABORT probe12 delivery"; exit 5; }
say "installed 2774 + probe12 (pool off boot)"
# 2. pool ON boot with 2774
boot $OUT $L "" "" 1
[ "$POOL" = 1 ] || { say "POOL NOT ARMED (hypervisor lines above); stop here"; exit 8; }
sleep 60
kit $L $LOG $OUT $KIT || exit $?
say "restart: second pool boot (KMD stop path through the pool window, now with 2774)"
boot $OUT $L "" "" 1
kit $L $LOG $OUT $KIT2 || exit $?
say "S266 CHAIN DONE link=$L out=$OUT pool=$POOL res=$RES"
